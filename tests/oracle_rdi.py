"""Compare the reader with RAD's dump of the same file (radbin --dump at the pinned commit).
usage: oracle_rdi.py <rdiq binary> <file.rdi> <file.rdi.dump.txt.gz>
Every vmap entry, unit line table entry, source line, name and string in the dump becomes a
lookup; the reader must answer each as the dump says."""
import bisect, gzip, re, subprocess, sys
rdiq, rdi, dump = sys.argv[1:]
VMAPS = {'unit_vmap': 17, 'scope_vmap': 25, 'global_variables_vmap': 28}
q, want = [], []
def ask(query, answer): q.append(query); want.append(answer)
section = sub = None; shadowed = 0
unit_table, tables, file_of_map, vmaps, last_line = {}, {}, {}, {}, {}
for line in gzip.open(dump, 'rt', encoding='utf-8', errors='surrogateescape'):
    line = line.rstrip('\n')
    if m := re.match(r'(\w+):$', line): section, sub = m[1], None; continue
    if m := re.match(r'  // \w+\[(\d+)\]', line): sub = int(m[1]); continue
    if section in VMAPS and (m := re.match(r'  \{0x([0-9a-f]+) => (\d+)\}$', line)):
        vmaps.setdefault(section, []).append((int(m[1], 16), m[2]))
    elif section == 'units' and (m := re.match(r'    line_table_idx: (\d+)', line)): unit_table[sub] = int(m[1])
    elif section == 'line_tables' and (m := re.match(r'    \{ \[0x([0-9a-f]+), 0x([0-9a-f]+)\) file: (\d+), line: (\d+) \}$', line)):
        t = tables.setdefault(sub, []); first = int(m[1], 16)
        if t and t[-1][0] == first: t.pop(); shadowed += 1   # equal offsets: the last entry holds the range
        t.append((first, int(m[2], 16), f'{m[3]} {m[4]}'))
    elif section == 'source_files' and (m := re.match(r'  \{ file_path_node_idx: +\d+, source_line_map: +(\d+),.*// source_file\[(\d+)\]', line)):
        file_of_map[int(m[1])] = int(m[2])
    elif section == 'source_line_maps' and (m := re.match(r'    (\d+): \((.*)\)$', line)):
        voffs = ''.join(f' {int(v, 16)}' for v in m[2].split(', '))
        ask(f's {file_of_map[sub]} {m[1]}', voffs)
        if last_line.get(sub, 0) < int(m[1]) - 1: ask(f's {file_of_map[sub]} {int(m[1]) - 1}', voffs)   # a line with no code answers with the next line that has some
        last_line[sub] = int(m[1])
    elif section == 'name_maps' and (m := re.match(r'      "(.*)": ([\d, ]+?)( // idx_run.*)?$', line)) and '\\' not in m[1] and m[1]:
        ask(f'n {sub} x{m[1].encode("utf-8", "surrogateescape").hex()}', ''.join(f' {v}' for v in m[2].split(', ')))
    elif section == 'strings' and (m := re.match(r'  "(.*)" // string\[(\d+)\]$', line)) and '\\' not in m[1]:
        ask(f't {m[2]}', m[1])
# A vmap answers for the last key at or below the offset; its last entry only marks the end.
# RAD's converter writes some vmaps out of order (its reader's search assumes order), so no
# answer is defined there: such a table is counted and skipped, not compared.
unsorted = {}
for name, v in vmaps.items():
    descents = sum(1 for a, b in zip(v, v[1:]) if b[0] <= a[0])
    if descents: unsorted[name] = f'{descents} of {len(v)}'; continue
    for (voff, idx), (after, _) in zip(v, v[1:]):
        ask(f'v {VMAPS[name]} {voff}', idx); ask(f'v {VMAPS[name]} {after - 1}', idx)
    ask(f'v {VMAPS[name]} {v[-1][0]}', '0')
keys = [k for k, _ in vmaps['unit_vmap']]
def unit_at(voff):
    i = bisect.bisect_right(keys, voff) - 1
    return int(vmaps['unit_vmap'][i][1]) if 0 <= i < len(keys) - 1 else 0
lines = 0
for unit, t in unit_table.items():
    for first, opl, answer in tables.get(t, []):
        if opl <= first or opl - first > 1 << 32: continue     # the table's end marker
        for voff in {first, opl - 1}:
            if unit_at(voff) == unit: ask(f'l {voff}', answer); lines += 1
got = subprocess.run([rdiq, rdi], input='\n'.join(q) + '\n', capture_output=True, text=True, encoding='utf-8', errors='surrogateescape').stdout.split('\n')[:-1]
bad = [(a, w, g) for a, w, g in zip(q, want, got) if w != g]
print(f'oracle: {len(q)} lookups ({lines} line lookups), {len(got)} answers, {len(bad)} differ; tables RAD wrote out of order, skipped: {unsorted}; zero-length line entries where RAD answers differently at the exact offset: {shadowed}')
for b in bad[:10]: print('  query %s: dump says [%s], reader says [%s]' % b)
sys.exit(1 if bad or len(got) != len(q) or len(q) < 1000 else 0)
