"""oracle_pdb.py <pdbq binary> <file.pdb>...: what dbg/pdb.c reads from each file must be what
llvm-pdbutil prints for it: every module, line record, section contribution and section header."""
import re, subprocess, sys
pdbq, total = sys.argv[1], 0
for pdb in sys.argv[2:]:
    text = subprocess.run(['llvm-pdbutil', 'dump', '--modules', '--section-contribs', '--section-headers', '-l', pdb],
                          capture_output=True, text=True, errors='surrogateescape').stdout
    want, part, sec, va = [], '', [], {}
    lines = text.split('\n')
    for i, line in enumerate(lines):
        if re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines) *$', line): part = line.strip()
        elif part == 'Section Headers' and (m := re.match(r' +SECTION HEADER #(\d+)', line)):
            f = [lines[i + k].split()[0] for k in range(1, 6)]   # name (llvm-pdbutil reads on past a name of 8 bytes), virtual size, address, raw size, raw pointer
            flags = lines[i + 10].split()[0]
            va[int(m[1])] = int(f[2], 16)
            sec.append('section %s %s %s' % (m[1], ' '.join([f[0][:8]] + ['%x' % int(v, 16) for v in f[1:]]), '%x' % int(flags, 16)))
    def sc(line):
        m = re.search(r'mod = (\d+), (\d+):(\d+), size = (-?\d+)', line)
        return int(m[1]), int(m[2]), int(m[3]), int(m[4]) & 0xffffffff
    part = ''
    for i, line in enumerate(lines):
        if re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines) *$', line): part = line.strip()
        elif part == 'Modules' and (m := re.match(r' *Mod (\d+) \| `(.*)`: *$', line)):
            j = i + 1
            while not lines[j].lstrip().startswith('Obj:'): j += 1
            obj = re.match(r' *Obj: `(.*)`: *$', lines[j])[1]
            stream = re.search(r'debug stream: (\d+)', lines[j + 1])[1]
            want.append('module %d stream %s first %d:%d:%d:%d name %s obj %s' % ((int(m[1]), stream) + sc(lines[i + 1]) + (m[2], obj)))
        elif part == 'Section Contributions' and 'SC[' in line:
            c = sc(line)
            want.append('contrib %d:%d:%d:%d voff %x' % (c + ((va[c[1]] + c[2]) if c[1] in va else 0,)))
        elif part == 'Lines' and (m := re.match(r' *Mod (\d+) \| `', line)): mod, src = int(m[1]), ''
        elif part == 'Lines' and (m := re.match(r'(\S.*) \((?:([\w-]+): ([0-9A-F]+)|no checksum)\)$', line)):
            src = '%s %d:%s' % (m[1], {None: 0, 'MD5': 1, 'SHA-1': 2, 'SHA-256': 3}[m[2]], m[3] or '')
        elif part == 'Lines' and (m := re.match(r'  ([0-9A-F]{4}):([0-9A-F]{8})-([0-9A-F]{8}), line/(?:column/)?addr entries = (\d+)', line)):
            base, j, entries = va[int(m[1], 16)], i + 1, []
            while len(entries) < int(m[4]):
                # llvm-pdbutil names the two made-up lines of "just my code": never and always step into
                entries += [({'NSI': 0xf00f00, 'ASI': 0xfeefee}.get(l) or l, o) for l, o in re.findall(r'(\d+|NSI|ASI) ([0-9A-F]{8})', lines[j])]; j += 1
            want.append('lines %d %s %x-%x%s' % (mod, src, base + int(m[2], 16), base + int(m[3], 16), ''.join(' %s@%x' % (l, base + int(o, 16)) for l, o in entries)))
    want += sec
    got = subprocess.run([pdbq, pdb], capture_output=True, text=True, errors='surrogateescape').stdout.split('\n')[:-1]
    if got != want:
        bad = [(g, w) for g, w in zip(got + [''] * len(want), want + [''] * len(got)) if g != w]
        print(f'oracle_pdb: {pdb}: {len(got)} records read, llvm-pdbutil has {len(want)}; first difference:\n  ours   {bad[0][0]!r}\n  theirs {bad[0][1]!r}')
        sys.exit(1)
    total += len(want)
print(f'oracle_pdb: {total} modules, line blocks, contributions and sections of {len(sys.argv) - 2} files agree with llvm-pdbutil')
sys.exit(0 if total >= 100 else 1)
