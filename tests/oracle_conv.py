"""oracle_conv.py <pdb2rdi> <rdiq> <file.pdb>...: convert each PDB and ask the result, through the
reader, what llvm-pdbutil says about the PDB: the line at every offset where a line record starts
and at the last byte before the next, the offsets of every source line, the unit at both ends of
every section contribution, every section, and every procedure, block and inlined call with the scopes
around it. Compared by meaning (paths, names, offsets), not by table index. Where RAD's own conversion
of the PDB sits beside it (<file>.rdi), the line at every offset of the code and every scope must
also equal RAD's, except what an inlined call covers (RAD reads the annotations differently,
and its calls then overlap; lessons, L26). In our file the scope the vmap gives at every offset must be the innermost
scope that covers it (RAD's own vmap is not sorted, so it cannot be the reference for that)."""
import collections, os, re, subprocess, sys, tempfile
pdb2rdi, rdiq = sys.argv[1:3]
def run(rdi, queries):
    return subprocess.run([rdiq, rdi], input='\n'.join(queries) + '\n', capture_output=True, text=True, errors='surrogateescape').stdout.split('\n')[:-1]
def scopes(rdi):
    """per scope: parent, inline site, the offsets it covers (pieces without bytes left out), [thunk, container flags, is a root],
    name, link name, first child, next sibling, procedure, name of the procedure's unit ('' for a procedure made from a public
    symbol, which RAD files under a unit of its own), and for an inlined call (the name of the inlined function, what its own
    line table answers at each offset it names)"""
    unit, out = {}, [(0, 0, (), [], '', '', 0, 0, 0, '', None)]
    for line in run(rdi, [f'u {i}' for i in range(1, int(run(rdi, ['N 16'])[0]))]):
        first, count, name = line.split(' ', 2)
        for p in range(int(first), int(first) + int(count)): unit[p] = name.replace('*global*', '')
    for line in run(rdi, [f'D {i}' for i in range(1, int(run(rdi, ['N 23'])[0]))]):
        place, flags, name, link, site, site_lines = line.split('\t')
        parent, inline, child, sibling, proc, *ranges = place.split()
        ranges = tuple(v for i in range(0, len(ranges), 2) if ranges[i] != ranges[i + 1] for v in (int(ranges[i], 16), int(ranges[i + 1], 16)))
        out.append((int(parent), int(inline), ranges, flags.split(), name, link, int(child), int(sibling), int(proc), unit.get(int(proc), ''), (site, site_lines) if int(inline) else None))
    return out
def described(all_scopes, key, calls=True):
    """how often each description occurs; a description is what a scope covers, what the scopes around it cover, its unit and
    key(flags, name, link, inlined function). Without `calls`, what an inlined call covers is left out."""
    covers = lambda s: s[2] if calls or not s[1] else 'inline'
    out = collections.Counter()
    for s in all_scopes[1:]:
        chain, parent = [], s[0]
        while parent:
            chain.append(covers(all_scopes[parent]))
            parent = all_scopes[parent][0]
        out[(covers(s), tuple(chain), s[9], *key(s[3], s[4], s[5], s[10]))] += 1
    return out
def normal(src, folder):
    """a source path as the converter's path tree has it: a relative one starts from the folder of the module's object file,
    "." stays out, ".." goes up"""
    parts = []
    for k, p in enumerate(re.split(r'[/\\]', src if re.match(r'[/\\]|.:', src) or not src else folder + src)):
        if p == '..' and parts: parts.pop()
        elif p != '.' and (p or k == 0): parts.append(p)
    return '/'.join(parts)
total = 0
for pdb in sys.argv[3:]:
    text = subprocess.run(['llvm-pdbutil', 'dump', '--modules', '--section-contribs', '--section-headers', '-l', '--il', '--files', '--symbols', pdb],
                          capture_output=True, text=True, errors='surrogateescape').stdout
    lines = text.split('\n')
    part, va, q, want = '', {}, [], []
    def ask(query, answer): q.append(query); want.append(answer)
    recs, per_line, mods, order, src, folder, spans, syms, expected, file_at, starts = [], {}, {}, 0, '', '', [], {}, collections.Counter(), {}, {}
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines|Inlinee Lines|Files|Symbols) *$', line): part = m[1]
        elif part == 'Section Headers' and (m := re.match(r' +SECTION HEADER #(\d+)', line)):
            f = [lines[i + k].split()[0] for k in range(1, 6)]
            name, vsize, addr, fsize, foff = f[0], *(int(v, 16) for v in f[1:])
            flags = int(lines[i + 10].split()[0], 16)
            va[int(m[1])] = addr
            if flags >> 29 & 1: spans.append((addr, vsize))
            ask(f'B {m[1]}', '%s %d %x %x %x %x' % (name, (flags >> 30 & 1) | (flags >> 31 & 1) << 1 | (flags >> 29 & 1) << 2, addr, addr + vsize, foff, foff + fsize))
        elif part in ('Files', 'Inlinee Lines') and (m := re.match(r' *Mod (\d+) \| `(.*)`: *$', line)): mod, folder, at = int(m[1]), re.sub(r'[^/\\]*$', '', m[2]), 0
        elif part == 'Files' and (m := re.match(r'- \((?:[\w-]+: ([0-9A-F]+)|no checksum)\) (.*)$', line)):
            # a module's checksum records in order: 6 bytes and the checksum, rounded up to 4; a file is named by its record's offset
            file_at[mod, at] = normal(m[2], folder); at += (6 + len(m[1] or '') // 2 + 3) // 4 * 4
        elif part == 'Inlinee Lines' and (m := re.match(r' +0x([0-9A-F]+) \| +(\d+) \| (.*) \((?:[\w-]+: [0-9A-F]+|no checksum)\)$', line)):
            starts[mod, int(m[1], 16)] = (normal(m[3], folder), int(m[2]))
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines|Inlinee Lines|Files|Symbols) *$', line): part = m[1]
        elif part == 'Modules' and (m := re.match(r' *Mod (\d+) \| `(.*)`: *$', line)): mods[int(m[1])] = re.split(r'[/\\]', m[2])[-1] or m[2]
        elif part == 'Section Contributions' and (m := re.search(r'mod = (\d+), (\d+):(\d+), size = (-?\d+)', line)):
            sec, size = int(m[2]), int(m[4])
            if sec in va and size > 0:
                for voff in (va[sec] + int(m[3]), va[sec] + int(m[3]) + size - 1): ask(f'U {voff}', mods[int(m[1])])
        elif part == 'Symbols' and (m := re.match(r' *Mod (\d+) \| `', line)): syms, mod, modn = {}, mods[int(m[1])], int(m[1])
        elif part == 'Symbols' and (m := re.match(r' *(\d+) \| (S_GPROC32|S_LPROC32|S_THUNK32|S_BLOCK32) \[size = \d+\] `(.*)`$', line)):
            # a procedure, a thunk, or a block that names the record it is inside: (what it covers, what is around it, unit, external, name, inlined function)
            p = re.match(r' +parent = (\d+), end = \d+(?:, addr = (\d+):(\d+), code size = (\d+))?', lines[i + 1])
            sec, off, size = map(int, p.groups()[1:] if p[2] else re.search(r'size = (\d+), addr = (\d+):(\d+)', lines[i + 2]).group(2, 3, 1))
            covers = (va[sec] + off, va[sec] + off + size) if sec in va and size else ()
            if m[2] == 'S_BLOCK32': around = syms.get(int(p[1]), ((), (), mod, 0))
            else: around, base = ((), (), mod, 0x80 * (m[2] == 'S_GPROC32')), va.get(sec, 0) + off
            syms[int(m[1])] = (covers, (around[0],) + around[1] if m[2] == 'S_BLOCK32' else (), mod, around[3], m[3] if m[2] != 'S_BLOCK32' else None, None)
            expected[syms[int(m[1])]] += 1
        elif part == 'Symbols' and (m := re.match(r' *(\d+) \| S_INLINESITE2? \[', line)):
            # an inlined call covers, from the procedure's start, what llvm-pdbutil prints for its annotations: from the
            # first "code" since the last end (or from that end) to each "code end"; pieces that touch are one
            # Its lines: each "code" starts the line counted so far ("line", from the function's first line) in the file
            # set so far; each end of a piece ends the lines. At one offset the last wins, and a line beats an end.
            p, j, at, first, pieces = re.match(r' +inlinee = 0x([0-9A-F]+) \((.*)\), parent = (\d+)', lines[i + 1]), i + 2, base, None, []
            path, line0 = starts.get((modn, int(p[1], 16)), ('', None))
            step, events = 0, []
            while not re.match(r' *\d+ \| |\s*Mod |\s*$', lines[j]):
                if f := re.search(r'setfile ([0-9A-F]+)', lines[j]): path = file_at.get((modn, int(f[1], 16)), '')
                if f := re.search(r' line (-?\d+)', lines[j]): step = int(f[1])
                for end, x in re.findall(r'code (end )?0x([0-9A-F]+)', lines[j]):
                    at = base + int(x, 16)
                    if not end:
                        events.append((at, 1, len(events), f'{path} {(line0 or 0) + step}'))
                        if first is None: first = at
                    else: pieces.append((at if first is None else first, at)); first = None
                j += 1
            pieces += [(first, at)] * (first is not None)
            covers = []
            for a, b in sorted(pieces):
                if covers and a <= covers[-1]: covers[-1] = max(covers[-1], b)
                elif a < b: covers += [a, b]
            events = sorted(events + [(b, 0, 0, ' 0') for a, b in pieces]) if line0 is not None else []
            for v, is_line, _, text in events:
                if is_line: per_line.setdefault((text.rsplit(' ', 1)[0], int(text.rsplit(' ', 1)[1])), []).append(v)
            site_lines = ''.join(f'{v:x} {text};' for k, (v, _, _, text) in enumerate(events) if k + 1 == len(events) or events[k + 1][0] != v)
            around = syms.get(int(p[3]), ((), (), mod, 0))
            syms[int(m[1])] = (tuple(covers), (around[0],) + around[1], mod, around[3], None, (p[2], site_lines))
            expected[syms[int(m[1])]] += 1
        elif part == 'Lines' and (m := re.match(r' *Mod \d+ \| `(.*)`: *$', line)): folder = re.sub(r'[^/\\]*$', '', m[1])
        elif part == 'Lines' and (m := re.match(r'(\S.*) \((?:[\w-]+: [0-9A-F]+|no checksum)\)$', line)): src = m[1]
        elif part == 'Lines' and (m := re.match(r'  ([0-9A-F]{4}):([0-9A-F]{8})-([0-9A-F]{8}), line/(?:column/)?addr entries = (\d+)', line)):
            base, j, entries = va[int(m[1], 16)], i + 1, []
            while len(entries) < int(m[4]):
                entries += re.findall(r'(\d+) ([0-9A-F]{8})', lines[j]); j += 1
            path = normal(src, folder)
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
        got = run(rdi, q)
        ours = scopes(rdi)
        has = described(ours, lambda flags, name, link, site: (int(flags[1]) & 0x80, name if flags[2] == '1' else None,
                                                             site and (site[0] if len(site[0]) <= 32 else site[0][:32] + '...', site[1])))   # as llvm-pdbutil prints a long name
        for k in expected | has:
            if k[2]: q.append(f'scope {k}'); want.append(str(expected[k])); got.append(str(has[k]))   # not the procedures made from public symbols
        # the list of a scope's children is the scopes that name it as parent, in order
        inside = collections.defaultdict(list)
        for k, s in enumerate(ours):
            if s[0]: inside[s[0]].append(k)
        for k, s in enumerate(ours):
            listed, at = [], s[6]
            while at and len(listed) < len(ours): listed.append(at); at = ours[at][7]
            if listed or inside[k]: q.append(f'children of scope {k}'); want.append(str(inside[k])); got.append(str(listed))
            if not s[0] and s[7]: q.append(f'next sibling of scope {k}, which has no parent'); want.append('0'); got.append(str(s[7]))
        # the name maps give the procedures of each name and of each link name, and none for the empty name
        for kind, field in ((4, 4), (6, 5)):
            by_name = collections.defaultdict(list, {'': []})
            for s in ours:
                if s[3] and s[3][2] == '1' and s[field]: by_name[s[field]].append(s[8])
            names = [f'n {kind} x{name.encode("utf-8", "surrogateescape").hex()}' for name in by_name]
            q += names; want += [''.join(f' {p}' for p in v) for v in by_name.values()]; got += run(rdi, names)
        # the innermost scope at each offset: paint wide ranges first; among equal ranges the later scope wins
        pieces = sorted((r[i] - r[i + 1], k, r[i]) for k, s in enumerate(ours) for r in [s[2]] for i in range(0, len(r), 2))
        lo, hi = min((p[2] for p in pieces), default=0), max((p[2] - p[0] for p in pieces), default=0)
        inner = [0] * (hi + 1 - lo)
        for size, k, first in pieces: inner[first - lo:first - lo - size] = [k] * -size
        vmap = [f'v 25 {v}' for v in range(lo, hi + 1)]
        q += vmap; want += map(str, inner); got += run(rdi, vmap)
        rad = pdb[:-4] + '.rdi'   # RAD's own conversion of the same PDB, where the runner recorded one: the line at every offset of the code
        if os.path.exists(rad):
            every = [f'L {v}' for a, n in spans for v in range(a, a + n)]
            q += every; want += run(rad, every); got += run(rdi, every)
            # NOT YET (types slice): the kind of container, and the container's name cut from the front of a procedure's name
            a, b = (described(s, lambda flags, name, link, site: (flags[0], int(flags[1]) & 0x80, name.split('::')[-1], link, site and site[0]), calls=False) for s in (ours, scopes(rad)))
            for k in a | b: q.append(f'scope, as RAD has it, {k}'); want.append(str(b[k])); got.append(str(a[k]))
    bad = [(a, w, g) for a, w, g in zip(q, want, got) if w != g]
    if bad or len(got) != len(q):
        print(f'oracle_conv: {pdb}: {len(q)} questions, {len(got)} answers, {len(bad)} differ')
        for b in bad[:8]: print('  %s: expected [%s], our file says [%s]' % b)
        sys.exit(1)
    total += len(q)
print(f'oracle_conv: {total} answers from {len(sys.argv) - 3} converted files agree with llvm-pdbutil and, where recorded, with RAD\'s conversion')
sys.exit(0 if total >= 1000 else 1)
