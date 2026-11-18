"""oracle_conv.py <pdb2rdi> <rdiq> <file.pdb>...: convert each PDB and ask the result, through the
reader, what llvm-pdbutil says about the PDB: the line at every offset where a line record starts
and at the last byte before the next, the offsets of every source line, the unit at both ends of
every section contribution, and every section. Compared by meaning (paths, names), not by table
index."""
import os, re, subprocess, sys, tempfile
pdb2rdi, rdiq = sys.argv[1:3]
total = 0
for pdb in sys.argv[3:]:
    text = subprocess.run(['llvm-pdbutil', 'dump', '--modules', '--section-contribs', '--section-headers', '-l', pdb],
                          capture_output=True, text=True, errors='surrogateescape').stdout
    lines = text.split('\n')
    part, va, q, want = '', {}, [], []
    def ask(query, answer): q.append(query); want.append(answer)
    recs, per_line, mods, order, src, folder, spans = [], {}, {}, 0, '', '', []
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines) *$', line): part = m[1]
        elif part == 'Section Headers' and (m := re.match(r' +SECTION HEADER #(\d+)', line)):
            f = [lines[i + k].split()[0] for k in range(1, 6)]
            name, vsize, addr, fsize, foff = f[0], *(int(v, 16) for v in f[1:])
            flags = int(lines[i + 10].split()[0], 16)
            va[int(m[1])] = addr
            if flags >> 29 & 1: spans.append((addr, vsize))
            ask(f'B {m[1]}', '%s %d %x %x %x %x' % (name, (flags >> 30 & 1) | (flags >> 31 & 1) << 1 | (flags >> 29 & 1) << 2, addr, addr + vsize, foff, foff + fsize))
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines) *$', line): part = m[1]
        elif part == 'Modules' and (m := re.match(r' *Mod (\d+) \| `(.*)`: *$', line)): mods[int(m[1])] = re.split(r'[/\\]', m[2])[-1] or m[2]
        elif part == 'Section Contributions' and (m := re.search(r'mod = (\d+), (\d+):(\d+), size = (-?\d+)', line)):
            sec, size = int(m[2]), int(m[4])
            if sec in va and size > 0:
                for voff in (va[sec] + int(m[3]), va[sec] + int(m[3]) + size - 1): ask(f'U {voff}', mods[int(m[1])])
        elif part == 'Lines' and (m := re.match(r' *Mod \d+ \| `(.*)`: *$', line)): folder = re.sub(r'[^/\\]*$', '', m[1])
        elif part == 'Lines' and (m := re.match(r'(\S.*) \((?:[\w-]+: [0-9A-F]+|no checksum)\)$', line)): src = m[1]
        elif part == 'Lines' and (m := re.match(r'  ([0-9A-F]{4}):([0-9A-F]{8})-([0-9A-F]{8}), line/(?:column/)?addr entries = (\d+)', line)):
            base, j, entries = va[int(m[1], 16)], i + 1, []
            while len(entries) < int(m[4]):
                entries += re.findall(r'(\d+) ([0-9A-F]{8})', lines[j]); j += 1
            parts = []        # "." stays, ".." goes up, as the converter's path tree does
            # a relative path is taken from the folder of the module's object file
            for k, p in enumerate(re.split(r'[/\\]', src if re.match(r'[/\\]|.:', src) or not src else folder + src)):
                if p == '..' and parts: parts.pop()
                elif p != '.' and (p or k == 0): parts.append(p)
            path = '/'.join(parts)
            for l, o in entries:
                voff = base + int(o, 16)
                recs.append((voff, 1, order, f'{path} {l}')); order += 1
                per_line.setdefault((path, int(l)), []).append(voff)
            recs.append((base + int(m[3], 16), 0, order, ' 0')); order += 1
    # the rule the converter follows: at one offset the last record wins, and a line beats the end of a block
    # (over all modules together: a function often starts where another module's function ends)
    recs.sort()
    last = [x for k, x in enumerate(recs) if k + 1 == len(recs) or recs[k + 1][0] != x[0]]
    for a, b in zip(last, last[1:]):
        ask(f'L {a[0]}', a[3]); ask(f'L {b[0] - 1}', a[3])
    if last: ask(f'L {last[-1][0]}', ' 0')
    for (path, l), voffs in per_line.items():
        ask(f'S {l} x{path.lower().encode("utf-8", "surrogateescape").hex()}', ''.join(f' {v}' for v in sorted(voffs)))
        if (path, l + 1) not in per_line: ask(f'S {l + 1} x{path.lower().encode("utf-8", "surrogateescape").hex()}', '')
    with tempfile.TemporaryDirectory() as d:
        rdi = os.path.join(d, 'out.rdi')
        if subprocess.run(['timeout', '120', pdb2rdi, pdb, rdi]).returncode != 0: sys.exit(f'oracle_conv: {pdb} does not convert (or takes over 2 minutes)')
        got = subprocess.run([rdiq, rdi], input='\n'.join(q) + '\n', capture_output=True, text=True, errors='surrogateescape').stdout.split('\n')[:-1]
        rad = pdb[:-4] + '.rdi'   # RAD's own conversion of the same PDB, where the runner recorded one: the line at every offset of the code
        if os.path.exists(rad):
            every = [f'L {v}' for a, n in spans for v in range(a, a + n)]
            ours, theirs = (subprocess.run([rdiq, f], input='\n'.join(every) + '\n', capture_output=True, text=True, errors='surrogateescape').stdout.split('\n')[:-1] for f in (rdi, rad))
            q += every; want += theirs; got += ours
    bad = [(a, w, g) for a, w, g in zip(q, want, got) if w != g]
    if bad or len(got) != len(q):
        print(f'oracle_conv: {pdb}: {len(q)} questions, {len(got)} answers, {len(bad)} differ')
        for b in bad[:8]: print('  %s: expected [%s], our file says [%s]' % b)
        sys.exit(1)
    total += len(q)
print(f'oracle_conv: {total} answers from {len(sys.argv) - 3} converted files agree with llvm-pdbutil and, where recorded, with RAD\'s conversion')
sys.exit(0 if total >= 1000 else 1)
