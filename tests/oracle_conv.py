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
    symbol, which RAD files under a unit of its own), for an inlined call (the name of the inlined function, what its own
    line table answers at each offset it names, its type and the class it is a method of), and its local variables"""
    unit, out = {}, [(0, 0, (), [], '', '', 0, 0, 0, '', None, '')]
    for line in run(rdi, [f'u {i}' for i in range(1, int(run(rdi, ['N 16'])[0]))]):
        first, count, name = line.split(' ', 2)
        for p in range(int(first), int(first) + int(count)): unit[p] = name.replace('*global*', '')
    count = int(run(rdi, ['N 23'])[0])
    for line, local in zip(run(rdi, [f'D {i}' for i in range(1, count)]), run(rdi, [f'V {i}' for i in range(1, count)])):
        place, flags, name, link, site, site_type, site_lines = line.split('\t')
        parent, inline, child, sibling, proc, *ranges = place.split()
        ranges = tuple(v for i in range(0, len(ranges), 2) if ranges[i] != ranges[i + 1] for v in (int(ranges[i], 16), int(ranges[i + 1], 16)))
        out.append((int(parent), int(inline), ranges, flags.split(), name, link, int(child), int(sibling), int(proc), unit.get(int(proc), ''), (site, site_lines, site_type) if int(inline) else None, local))
    return out
def described(all_scopes, key, local, calls=True):
    """how often each description occurs; a description is what a scope covers, what the scopes around it cover, its unit,
    key(flags, name, link, inlined function) and local(its local variables). Without `calls`, what an inlined call covers is left out."""
    covers = lambda s: s[2] if calls or not s[1] else 'inline'
    out = collections.Counter()
    for s in all_scopes[1:]:
        chain, parent = [], s[0]
        while parent:
            chain.append(covers(all_scopes[parent]))
            parent = all_scopes[parent][0]
        out[(covers(s), tuple(chain), s[9], *key(s[3], s[4], s[5], s[10]), local(s[11]))] += 1
    return out
def tail(name):
    """a C++ name without its containers: what follows the last "::" outside angle brackets.
    NOT YET (namespaces): RAD cuts the container off a procedure's and a type's name and records it apart."""
    depth = cut = 0
    for i, c in enumerate(name):
        depth += (c == '<') - (c == '>')
        if depth == 0 and name[i:i + 2] == '::': cut = i + 2
    return name[cut:]
def typed(rdi, table, op):
    """how often each (name, type by meaning) occurs among the records of a table. RAD calls `int` int32 and
    `unsigned int` uint32 (its table of basic types has both names for one code, and the later wins); we do not.
    RAD reads no member after a virtual base (it does not step over the base's two numbers), so members are compared
    up to the first one. Left out: what names subjects/kinds.cpp's Odd, whose methods and member pointers RAD reads
    differently (knowledge base, 04: the `this` of an `&&` method, a `...` parameter, pointers to members)."""
    out = collections.Counter()
    for line in run(rdi, [f'{op} {i}' for i in (range(1, int(run(rdi, [f'N {table}'])[0])) if isinstance(table, int) else table)]):
        name, kind = line.split('\t', 1) if op[0] in 'PGO' else ('', line)
        kind = re.sub(r'2004 (u?)int32:4 on ', lambda m: '2004 %sint:4 on ' % ('unsigned ' * bool(m[1])), kind)
        if op == 'M': kind = re.sub(r'( 202\+0 [^;]*; ).*', r'\1', kind)
        if 'Odd' not in line: out[tail(name), re.sub(r'(20\d\d) (.*?):(\d+)', lambda m: f'{m[1]} {tail(m[2])}:{m[3]}', kind)] += 1
    return out
BASIC = {0x10: ('signed char', 'Char8', 1), 0x11: ('short', 'S16', 2), 0x12: ('long', 'S32', 4), 0x13: ('long long', 'S64', 8), 0x20: ('unsigned char', 'UChar8', 1),
         0x21: ('unsigned short', 'U16', 2), 0x22: ('unsigned long', 'U32', 4), 0x23: ('unsigned long long', 'U64', 8), 0x30: ('bool', 'S8', 1), 0x40: ('float', 'F32', 4),
         0x41: ('double', 'F64', 8), 0x70: ('char', 'Char8', 1), 0x71: ('wchar_t', 'UChar16', 2), 0x74: ('int', 'S32', 4), 0x75: ('unsigned int', 'U32', 4),
         0x76: ('__int64', 'S64', 8), 0x77: ('__uint64', 'U64', 8), 0x7a: ('char16_t', 'Char16', 2), 0x7b: ('char32_t', 'Char32', 4), 0x03: ('', 'Void', 0), 0x08: ('', 'HResult', 4)}
def meanings(pdb):
    """each type id's meaning, as rdiq prints a type, and its size, from llvm-pdbutil's reading of the type records (64-bit files),
    taken in order: a record that names itself or a later one has no type there. A forward reference is the definition
    llvm-pdbutil found for it, wherever that is. No types when the ids do not start at 0x1000."""
    recs, memo, forward, funcs = {}, {}, [], {}
    for line in subprocess.run(['llvm-pdbutil', 'dump', '--types', pdb], capture_output=True, text=True, errors='surrogateescape').stdout.split('\n'):
        if m := re.match(r' +(0x[0-9A-F]+) \| (LF_\w+) \[size = \d+\](?: `(.*)`)?$', line): rec = recs[int(m[1], 16)] = [m[2], m[3] or '', '']
        elif recs: rec[2] += line + '\n'
    def of(i):
        if i not in memo and (i < 0x1000 or forward): memo[i] = make(i)
        return memo.get(i, ('none', 0))
    def make(i):
        if 0xff < i < 0x1000: return '1001.0[0]:8 ' + of(i & 0xff)[0], 8
        if i in BASIC: c, b, n = BASIC[i]; return (f'2004 {c}:{n} on {b}:{n}' if c else f'{b}:{n}'), n
        kind, name, body = recs.get(i, ('', '', ''))
        def field(label): m = re.search(label + r' (0x[0-9A-F]+|<no type>|\d+)', body); return int(m[1], 0) if m and m[1][0] != '<' else 0
        def flagged(text, on):
            flags = sum(bit for word, bit in (('const', 1), ('volatile', 2), ('restrict', 4)) if word in re.split(r'[ |]+', text))
            return (f'1000.{flags:x}[0]:{on[1]} ' if flags else '') + on[0], on[1]
        if kind == 'LF_MODIFIER': return flagged(re.search(r'modifiers = (.*)', body)[1], of(field('referent =')))
        if kind == 'LF_POINTER':
            mode, opts = re.search(r'mode = ([a-z ]+), opts = ([^,]*),', body).groups()
            if mode not in ('pointer', 'ref', 'rvalue ref'): return 'none', 0   # pointers to members
            return flagged(opts, ({'pointer': '1001', 'ref': '1002', 'rvalue ref': '1003'}[mode] + '.0[0]:8 ' + of(field('referent ='))[0], 8))
        if kind in ('LF_PROCEDURE', 'LF_MFUNCTION'):
            args = [int(x, 16) for x in re.findall(r'^ +(0x[0-9A-F]+)[ :]', recs.get(field('param list ='), ('', '', ''))[2], re.M)]
            params = [of(x)[0] for x in ([field('this type =')] if field('this type =') else []) + [x for x in args if x]]
            funcs[i] = (of(field('return type ='))[0], len(params))
            return f"{'1006' if field('this type =') else '1005'}.0[{len(params)}]:8 {of(field('return type ='))[0]}" + (f" ({', '.join(params)})" if params else ''), 8
        if kind == 'LF_BITFIELD': on = of(field('type =')); return f"bits {field('bit offset =')}+{field('# bits =')}:{on[1]} of {on[0]}", on[1]
        if kind == 'LF_ARRAY':
            on, size = of(field('element type:')), field('size:')
            return f'1004.0[{size // on[1] if on[1] else 0}]:{size} {on[0]}', size
        if kind not in ('LF_STRUCTURE', 'LF_CLASS', 'LF_UNION', 'LF_ENUM'): return 'none', 0
        if m := re.search(r'forward ref \(\S+ (0x[0-9A-F]+)\)', body):
            if int(m[1], 16) != i: forward.append(i); full = of(int(m[1], 16)); forward.pop(); return full
            return f"{ {'LF_STRUCTURE': 2005, 'LF_UNION': 2006, 'LF_CLASS': 2007, 'LF_ENUM': 2008}[kind]} {name}:0", 0
        on = of(field('underlying type:')) if kind == 'LF_ENUM' else ('none', field('sizeof'))
        return f"{ {'LF_STRUCTURE': 2000, 'LF_CLASS': 2001, 'LF_UNION': 2002, 'LF_ENUM': 2003}[kind]} {name}:{on[1]}" + (f' on {on[0]}' if on[0] != 'none' else ''), on[1]
    if min(recs, default=0) != 0x1000: recs = {}
    for i in recs: memo.setdefault(i, make(i))
    def fields(i):
        """the members of a field list as rdiq prints them; a list that continues does so in an earlier one"""
        out = []
        for f in re.split(r'\n +- ', '\n' + recs.get(i, ('', '', ''))[2])[1:] if recs.get(i, ('',))[0] == 'LF_FIELDLIST' else []:
            kind = re.match(r'\w+', f)[0]
            def num(label): m = re.search(label + r' = (0x[0-9A-F]+|\d+)', f); return int(m[1], 0) if m else 0
            name = m[1] if (m := re.search(r'name = `(.*)`(?:, |\])', f)) else ''
            def method(text, t): return f"{name} {'102' if ' virtual' in text else '101' if ' static' in text else '100'}+0 {of(t)[0]}; "
            if kind == 'LF_INDEX': return out + (fields(num('continuation')) if num('continuation') < i else [])
            if kind == 'LF_ENUMERATE': m = re.match(r'LF_ENUMERATE \[(.*) = (-?\d+)\]', f); out.append(f'{m[1]}={int(m[2]) % 2**64}; ')
            elif kind == 'LF_MEMBER': out.append(f"{name} 1+{num('offset')} {of(num('Type'))[0]}; ")
            elif kind == 'LF_STMEMBER': out.append(f"{name} 2+0 {of(num('type'))[0]}; ")
            elif kind == 'LF_NESTTYPE': out.append(f"{name} 300+0 {of(num('parent'))[0]}; ")
            elif kind == 'LF_BCLASS': out.append(f" 201+{num('offset')} {of(num('type'))[0]}; ")
            elif kind in ('LF_VBCLASS', 'LF_IVBCLASS'): out.append(f" 202+0 {of(num('base'))[0]}; ")
            elif kind == 'LF_ONEMETHOD': out.append(method(f, num('type')))
            elif kind == 'LF_METHOD': out += [method(x[1], int(x[0], 16)) for x in re.findall(r'Method \[type = (0x[0-9A-F]+)(.*)', recs.get(num('overload list'), ('', '', ''))[2])]
            elif kind != 'LF_VFUNCTAB': sys.exit(f'oracle_conv: {pdb}: a field of a kind the oracle does not know: {f}')
        return out
    udts = collections.Counter()
    for i, (kind, name, body) in recs.items():   # each full definition's record of members, as rdiq prints one
        if kind in ('LF_STRUCTURE', 'LF_CLASS', 'LF_UNION', 'LF_ENUM') and 'forward ref' not in body:
            m = fields(int(re.search(r'field list: (0x[0-9A-F]+|<no type>)', body)[1].replace('<no type>', '0'), 16))
            udts[f"{memo[i][0]}\t1 {int(kind == 'LF_ENUM' and bool(m))}\t{''.join(m)}"] += 1
    of.funcs = funcs   # per function type: what it returns, how many parameters
    return memo, of, udts
def normal(src, folder):
    """a source path as the converter's path tree has it: a relative one starts from the folder of the module's object file,
    "." stays out, ".." goes up"""
    parts = []
    for k, p in enumerate(re.split(r'[/\\]', src if re.match(r'[/\\]|.:', src) or not src else folder + src)):
        if p == '..' and parts: parts.pop()
        elif p != '.' and (p or k == 0): parts.append(p)
    return '/'.join(parts)
total = 0
def raw_gaps(pdb):
    """per (module, record offset) of an S_DEFRANGE record, its gaps (start, length) from the record's bytes: llvm-pdbutil 18 ends the
    printed list at the first byte from 0xf0 up, which it takes for padding (a gap that starts at 255 or 508 is lost, and all after it)"""
    out, mod, key = {}, 0, None
    for line in subprocess.run(['llvm-pdbutil', 'dump', '--symbols', '--sym-data', pdb], capture_output=True, text=True, errors='surrogateescape').stdout.split('\n'):
        if m := re.match(r' *Mod (\d+) \| `', line): mod = int(m[1])
        elif m := re.match(r' *(\d+) \| (\w+) \[', line):
            key = (mod, int(m[1])) if m[2].startswith('S_DEFRANGE_') else None
            if key: out[key] = [bytearray(), 16 if 'REGISTER_REL' in m[2] or 'SUBFIELD' in m[2] else 12]
        elif key and (m := re.match(r' +[0-9A-F]{4}: ((?:[0-9A-F]{2,8} ?)+) *\|', line)): out[key][0] += bytes.fromhex(m[1].replace(' ', ''))
    return {k: [(d[i] | d[i + 1] << 8, d[i + 2] | d[i + 3] << 8) for i in range(head, len(d) - 3, 4)] for k, (d, head) in out.items()}
for pdb in sys.argv[3:]:
    text = subprocess.run(['llvm-pdbutil', 'dump', '--modules', '--section-contribs', '--section-headers', '-l', '--il', '--files', '--symbols', '--globals', pdb],
                          capture_output=True, text=True, errors='surrogateescape').stdout
    lines = text.split('\n')
    gaps_of = raw_gaps(pdb) if 'S_DEFRANGE_' in text else {}
    part, va, q, want = '', {}, [], []
    def ask(query, answer): q.append(query); want.append(answer)
    recs, per_line, mods, order, src, folder, spans, syms, expected, file_at, starts, proc_types, data = [], {}, {}, 0, '', '', [], {}, collections.Counter(), {}, {}, collections.Counter(), ([], [])
    frames, made, at_proc, stack, cur = {}, [], None, [], {'target': None}   # per procedure record: its frame; per scope record: (its description, its locals)
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines|Inlinee Lines|Files|Symbols|Global Symbols) *$', line): part = m[1]
        elif part == 'Section Headers' and (m := re.match(r' +SECTION HEADER #(\d+)', line)):
            f = [lines[i + k].split()[0] for k in range(1, 6)]   # the name: llvm-pdbutil reads on past one of 8 bytes
            name, vsize, addr, fsize, foff = f[0][:8], *(int(v, 16) for v in f[1:])
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
        # a procedure's frame is the last S_FRAMEPROC before the next procedure: (size, the frame pointer for locals, for parameters)
        elif part == 'Symbols' and (m := re.match(r' *Mod (\d+) \| `', line)): mod, at_proc = int(m[1]), None
        elif part == 'Symbols' and (m := re.match(r' *(\d+) \| S_[GL]PROC32 \[', line)): at_proc = int(m[1])
        elif part == 'Symbols' and at_proc is not None and re.match(r' *\d+ \| S_FRAMEPROC \[', line):
            frames[mod, at_proc] = (int(re.search(r'size = (\d+)', lines[i + 1])[1]), *re.search(r'local fp reg = (\w+), param fp reg = (\w+)', lines[i + 3]).groups())
    for i, line in enumerate(lines):
        if m := re.match(r' +(Modules|Section Headers|Section Contributions|Original Section Headers|Lines|Inlinee Lines|Files|Symbols|Global Symbols) *$', line): part = m[1]
        elif part == 'Modules' and (m := re.match(r' *Mod (\d+) \| `(.*)`: *$', line)): mods[int(m[1])] = re.split(r'[/\\]', m[2])[-1] or m[2]
        elif part == 'Section Contributions' and (m := re.search(r'mod = (\d+), (\d+):(\d+), size = (-?\d+)', line)):
            sec, size = int(m[2]), int(m[4])
            if sec in va and size > 0:
                for voff in (va[sec] + int(m[3]), va[sec] + int(m[3]) + size - 1): ask(f'U {voff}', mods[int(m[1])])
        elif part == 'Symbols' and (m := re.match(r' *Mod (\d+) \| `', line)):
            syms, mod, modn, stack, cur = {}, mods[int(m[1])], int(m[1]), [], {'type': 0, 'opt': False, 'n': 0, 'frame': None, 'target': None}
        elif part == 'Symbols' and (m := re.match(r' *(\d+) \| (S_GPROC32|S_LPROC32|S_THUNK32|S_BLOCK32) \[size = \d+\] `(.*)`$', line)):
            # a procedure, a thunk, or a block that names the record it is inside: (what it covers, what is around it, unit, external, name, inlined function)
            p = re.match(r' +parent = (\d+), end = \d+(?:, addr = (\d+):(\d+), code size = (\d+))?', lines[i + 1])
            sec, off, size = map(int, p.groups()[1:] if p[2] else re.search(r'size = (\d+), addr = (\d+):(\d+)', lines[i + 2]).group(2, 3, 1))
            covers = (va[sec] + off, va[sec] + off + size) if sec in va and size else ()
            if m[2] == 'S_BLOCK32': around = syms.get(int(p[1]), ((), (), mod, 0))
            else: around, base = ((), (), mod, 0x80 * (m[2] == 'S_GPROC32')), va.get(sec, 0) + off
            syms[int(m[1])] = (covers, (around[0],) + around[1] if m[2] == 'S_BLOCK32' else (), mod, around[3], m[3] if m[2] != 'S_BLOCK32' else None, None)
            stack.append([]); made.append((syms[int(m[1])], stack[-1]))
            if t := re.match(r' +type = `(0x[0-9A-F]+)', lines[i + 2]): proc_types[m[3], int(t[1], 16)] += 1
            if 'PROC' in m[2]: cur = {'type': int(t[1], 16) if t else 0, 'opt': 'opt debuginfo' in lines[i + 2], 'n': 0, 'frame': frames.get((modn, int(m[1]))), 'target': None}
        # Locals, each in the innermost open scope, as (name, parameter, type id, cases) or for S_REGREL32 (name, its number in the procedure, type id,
        # register, offset, the procedure's type and frame). S_REGREL32 is not read in a procedure with S_LOCAL records. S_DEFRANGE records give
        # the cases of the S_LOCAL before them: (first, length, gaps, what kind of place, register or which frame pointer, offset).
        elif part == 'Symbols' and re.match(r' *\d+ \| (S_END|S_INLINESITE_END) \[', line): stack[-1:] = []; cur['target'] = None
        elif part == 'Symbols' and (m := re.match(r' *\d+ \| S_REGREL32 \[size = \d+\] `(.*)`$', line)):
            p = re.match(r' +type = (0x[0-9A-F]+|<no type>).*, register = (\w*), offset = (-?\d+)$', lines[i + 1])
            cur['n'] += 1
            if stack and not cur['opt']: stack[-1].append((m[1], cur['n'] - 1, int(p[1].replace('<no type>', '0'), 16), p[2], int(p[3]), cur['type'], cur['frame']))
        elif part == 'Symbols' and stack and (m := re.match(r' *\d+ \| S_LOCAL \[size = \d+\] `(.*)`$', line)):
            p = re.match(r' +type=(0x[0-9A-F]+|<no type>).*, flags = (.*)$', lines[i + 1])
            cur['target'] = None if 'enreg' in p[2] else (m[1], 'param' in p[2].split(' | '), int(p[1].replace('<no type>', '0'), 16), [])
            if cur['target']: stack[-1].append(cur['target'])
        elif part == 'Symbols' and re.match(r' *\d+ \| S_FILESTATIC \[', line): cur['target'] = None
        elif part == 'Symbols' and cur['target'] and (m := re.match(r' *(\d+) \| S_DEFRANGE_(\w+) \[size = \d+\](.*)$', line)):
            text = m[3] if m[2] == 'FRAMEPOINTER_REL_FULL_SCOPE' else lines[i + 1] + '\n' + lines[i + 2]
            r = re.search(r'(?:range start = |range = \[)(\d+):(\d+),(?: length = |\+)(\d+)', text) or [0, 0, 0, 0]
            reg, off, part_at = re.search(r'register = (\w+(?: \(\d+\))?|)', text), re.search(r'offset = (-?\d+)', text), re.search(r'offset in parent = (\d+)', text)
            frame = cur['frame'] and cur['frame'][1 + cur['target'][1]]
            where = ('reg', reg[1]) if m[2] == 'REGISTER' or (m[2] == 'SUBFIELD_REGISTER' and part_at[1] == '0') else ('at', reg[1], int(off[1])) if m[2] == 'REGISTER_REL' \
                    else ('at', frame, int(off[1])) if m[2].startswith('FRAMEPOINTER_REL') and frame else None
            first = va.get(int(r[1]), 0) + int(r[2]) if int(r[1]) in va else 0
            if where: cur['target'][3].append((first, int(r[3]), gaps_of[modn, int(m[1])] if m[2] != 'FRAMEPOINTER_REL_FULL_SCOPE' else None, *where))
        elif part in ('Symbols', 'Global Symbols') and (m := re.match(r' *(\d+) \| (S_[GL]DATA32|S_[GL]THREAD32|S_CONSTANT|S_UDT) \[size = \d+\] `(.*)`$', line)):
            # a variable, constant or typedef: (kind, name, type id, section, offset or value), module streams first as the converter
            # reads them, then the global stream in the order of its records (llvm-pdbutil prints it in the order of its hash table)
            p = re.match(r' +(?:original )?type = (0x[0-9A-F]+|<no type>).*?(?:, addr = (\d+):(\d+)|, value = (-?\d+))?$', lines[i + 1])
            data[part != 'Symbols'].append((int(m[1]), m[2], m[3], int(p[1].replace('<no type>', '0'), 16), int(p[2] or 0), int(p[3] or p[4] or 0), part != 'Symbols'))
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
            syms[int(m[1])] = (tuple(covers), (around[0],) + around[1], mod, around[3], None, (p[2], site_lines, int(p[1], 16)))
            stack.append([]); made.append((syms[int(m[1])], stack[-1]))
        elif part == 'Lines' and (m := re.match(r' *Mod \d+ \| `(.*)`: *$', line)): folder = re.sub(r'[^/\\]*$', '', m[1])
        elif part == 'Lines' and (m := re.match(r'(\S.*) \((?:[\w-]+: [0-9A-F]+|no checksum)\)$', line)): src = m[1]
        elif part == 'Lines' and (m := re.match(r'  ([0-9A-F]{4}):([0-9A-F]{8})-([0-9A-F]{8}), line/(?:column/)?addr entries = (\d+)', line)):
            base, j, entries = va[int(m[1], 16)], i + 1, []
            while len(entries) < int(m[4]):
                entries += [(str({'NSI': 0xf00f00, 'ASI': 0xfeefee}.get(l) or l), o) for l, o in re.findall(r'(\d+|NSI|ASI) ([0-9A-F]{8})', lines[j])]; j += 1
            path = normal(src, folder)
            for k, (l, o) in enumerate(entries):
                if k and l == entries[k - 1][0]: continue   # a record of the line before it is no row (dbg/conv.c)
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
        # each procedure with a type has it, by llvm-pdbutil's reading of the type records
        means, of, udts = meanings(pdb)
        # Every scope with its locals. A place is `3 register offset` (at register + offset), `4 ...` (at the address stored there), `5 register`
        # (in it), or bytecode when the offset does not fit 16 bits; a local's place is none, one place that holds everywhere, or a list of
        # [first-end place]. llvm-pdbutil prints the number of a register it has no name for: the parts of xmm0 to xmm7 (CV_AMD64_XMM0_0 = 162 and on: four floats each;
        # CV_AMD64_XMM0L = 194 and on: the low doubles).
        REG = {n: k + 1 for names in ('RAX RCX RDX RBX RSP RBP RSI RDI R8 R9 R10 R11 R12 R13 R14 R15', 'EAX ECX EDX EBX ESP EBP ESI EDI R8D R9D R10D R11D R12D R13D R14D R15D',
               'AX CX DX BX SP BP SI DI R8W R9W R10W R11W R12W R13W R14W R15W', 'AL CL DL BL SPL BPL SIL DIL R8B R9B R10B R11B R12B R13B R14B R15B', 'AH CH DH BH') for k, n in enumerate(names.split())}
        REG.update({f'XMM{k}': 41 + k for k in range(16)} | {f'unknown ({162 + k})': 41 + k // 4 for k in range(32)} | {f'unknown ({194 + k})': 41 + k for k in range(8)})
        def at(reg, off, indirect=False):
            off = (off + 2**31) % 2**32 - 2**31
            if -2**15 <= off < 2**15: return f'{4 if indirect else 3} {REG.get(reg, 0)} {off}'
            return f'1 5:{0x800 | REG.get(reg, 0):08x} 13:{off % 2**32:08x} 42:20 21:0000' + ' 4:08' * indirect
        def shown(local):
            if len(local) == 7:
                name, nth, t, reg, off, proc, frame = local
                param, size = nth < of.funcs.get(proc, ('', 0))[1], of(t)[1]
                back = reg in ('RSP', 'RBP') and frame is not None and off % 2**32 > frame[0] and t == 0 and of(proc)[0] != 'none'
                return f"{name} {int(param)} 2.1 {of.funcs.get(proc, ('none',))[0] if back else of(t)[0]} @ {at(reg, off, back or (param and (size > 8 or size & (size - 1) != 0)))}; "
            name, param, t, cases = local
            pieces = []
            for first, length, gaps, kind, reg, *off in cases:
                where = at(reg, *off) if kind == 'at' else f'5 {REG.get(reg, 0)}'
                cursor = first
                for a, b in gaps or []:
                    if cursor < first + a: pieces.append((cursor, first + a, where))
                    cursor = first + a + b
                if gaps is None: pieces.append((0, 2**64 - 1, where))
                elif cursor < first + length: pieces.append((cursor, first + length, where))
            lone = len(pieces) == 1 and pieces[0][:2] == (0, 2**64 - 1)
            return f"{name} {int(param)} 2.1 {of(t)[0]} @ {pieces[0][2] if lone else ''.join(f'[{a:x}-{b:x} {w}]' for a, b, w in pieces) or 'none'}; "
        # an inlined call's function: the id record's type, and for a method's id record its class
        ids = {int(m[1], 16): (int(m[2], 16), int(m[3] or '0', 16)) for m in re.finditer(r'(0x[0-9A-F]+) \| LF_M?FUNC_ID \[.*\]\n.*, type = (0x[0-9A-F]+)(?:, class type = (0x[0-9A-F]+))?',
               subprocess.run(['llvm-pdbutil', 'dump', '--ids', pdb], capture_output=True, text=True, errors='surrogateescape').stdout)}
        inlined = lambda site: site and site[:2] + (' of '.join(of(t)[0] for t in ids.get(site[2], (0, 0))),)
        for d, local in made: expected[d[:5] + (inlined(d[5]), ''.join(map(shown, local)))] += 1
        has = described(ours, lambda flags, name, link, site: (int(flags[1]) & 0x80, name if flags[2] == '1' else None,
                                                             site and (site[0] if len(site[0]) <= 32 else site[0][:32] + '...', site[1], site[2])), str)   # as llvm-pdbutil prints a long name
        for k in expected | has:
            if k[2]: q.append(f'scope {k}'); want.append(str(expected[k])); got.append(str(has[k]))   # not the procedures made from public symbols
        a = collections.Counter(line for line in run(rdi, [f'P {i}' for i in range(1, int(run(rdi, ['N 31'])[0]))]) if not line.endswith('\tnone'))
        b = collections.Counter(f'{name}\t{of(t)[0]}' for name, t in proc_types.elements() if of(t)[0] != 'none')
        for k in a | b: q.append(f'procedure and its type {k}'); want.append(str(b[k])); got.append(str(a[k]))
        # every type record's meaning is a type of ours; each type of ours is one, or what a const pointer or a basic type's name stands on
        # and each typedef of the global stream is a name on its type; the types of variables and constants are made too
        data = [x[1:] for x in data[0] + sorted(data[1])]
        for kind, _, t, _, _, in_globals in data:
            if kind != 'S_UDT' or in_globals: of(t)
        aliases = {f'2004 {name}:{of(t)[1]}' + (f' on {of(t)[0]}' if of(t)[0] != 'none' else '') for kind, name, t, _, _, in_globals in data if kind == 'S_UDT' and in_globals}
        a, b = set(run(rdi, [f'T {i}' for i in range(1, int(run(rdi, ['N 19'])[0]))])), {m for m, _ in means.values()} - {'none'} | aliases
        under = {re.sub(r'^(1000\.\w\[0\]:\d+|2004 [\w ]+:\d on) ', '', m) for m in b}
        for k in a | b: q.append(f'type {k}'); want.append('True'); got.append(str(k in a and k in b | under))
        # each struct, class, union and enum that is defined has its members
        a = collections.Counter(run(rdi, [f'M {i}' for i in range(1, int(run(rdi, ['N 20'])[0]))]))
        for k in a | udts: q.append(f'members {k}'); want.append(str(udts[k])); got.append(str(a[k]))
        # each variable and constant, by meaning; a constant's name has the containers of its type's name in front
        vars_, shown = [], collections.Counter()
        for kind, name, t, sec, off, in_globals in data:
            if kind == 'S_CONSTANT':
                named_type = re.match(r'20\d\d (.*?):\d+(?: on |$)', of(t)[0])
                full = (named_type[1][:len(named_type[1]) - len(tail(named_type[1]))] if named_type else '') + name
                if full: shown[f'30 {full}\t0 8 {off % 2**64:x}\t{of(t)[0]}'] += 1
            elif 'DATA' in kind:
                voff = va[sec] + off if sec in va else 0
                vars_.append((voff, of(t)[1], f'{name}\t{int(kind[2] == "G")} 6 {voff:x}\t{of(t)[0]}'))
                shown['27 ' + vars_[-1][2]] += 1
            elif 'THREAD' in kind: shown[f'29 {name}\t{int(kind[2] == "G")} 7 {off:x}\t{of(t)[0]}'] += 1
        a = collections.Counter(f'{sec} {line}' for sec in (27, 29, 30) for line in run(rdi, [f'G {sec} {i}' for i in range(1, int(run(rdi, [f'N {sec}'])[0]))]))
        for k in a | shown: q.append(f'variable {k}'); want.append(str(shown[k])); got.append(str(a[k]))
        # the name maps give the variables, constants and types of each name, and none for the empty name
        for kind, table, op in ((1, 27, 'G 27'), (2, 29, 'G 29'), (3, 30, 'G 30'), (5, 19, 'T')):
            by_name = collections.defaultdict(list, {'': []})
            for i, line in enumerate(run(rdi, [f'{op} {i}' for i in range(1, int(run(rdi, [f'N {table}'])[0]))]), 1):
                name = line.split('\t')[0] if op != 'T' else '' if re.match(r'1\w{3}\.|bits |none', line) else re.match(r'(?:2\w{3} )?(.*?):\d+(?: on |$)', line)[1]
                if name: by_name[name].append(i)
            names = [f'n {kind} x{name.encode("utf-8", "surrogateescape").hex()}' for name in by_name]
            q += names; want += [''.join(f' {p}' for p in v) for v in by_name.values()]; got += run(rdi, names)
        # the variable at an offset is, of those that cover it, the one that starts last: of two that start together the smaller, then the later
        # (MSVC's C runtime has a 16-byte table that the 320-byte _load_config_used starts inside)
        points = sorted({v for voff, size, _ in vars_ for v in ((voff, voff + size - 1, voff + size) if size else (voff,))})   # one without a type covers nothing
        for v in points:
            over = [(voff, -size, k, line) for k, (voff, size, line) in enumerate(vars_) if voff <= v < voff + size]
            q.append(f'O {v}'); want.append(max(over)[3] if over else '\t0 0 0\tnone')
        got += run(rdi, [f'O {v}' for v in points])
        # a basic type's name is made once
        every = collections.Counter(run(rdi, [f'T {i}' for i in range(1, int(run(rdi, ['N 19'])[0]))]))
        for k in every:
            if re.match(r'2004 [\w ]+:\d+ on \w+:\d+$', k) and k not in aliases: q.append(f'copies of type {k}'); want.append('1'); got.append(str(every[k]))
        rad = pdb[:-4] + '.rdi'   # RAD's own conversion of the same PDB, where the runner recorded one: the line at every offset of the code
        if os.path.exists(rad):
            every = [f'L {v}' for a, n in spans for v in range(a, a + n)]
            q += every; want += run(rad, every); got += run(rdi, every)
            # NOT YET (types slice): the kind of container, and the container's name cut from the front of a procedure's name
            # A local's lone place is the location itself in RAD's file even when it holds for a few bytes only (knowledge base, 04); ours is
            # then a list of one, compared as RAD has it. Types as in typed().
            # An inlined call's function type is compared; its class is not: RAD reads the first field of a plain function's id record as a
            # type too, where it is an id in the other stream (646 of 10,424 inlined calls in kinds_o2.pdb get a class that way).
            def as_rad(local):
                local = re.sub(r'@ \[[0-9a-f]+-[0-9a-f]+ ([^\]\[]*)\]; ', r'@ \1; ', re.sub(r'2004 (u?)int32:4 on ', lambda m: '2004 %sint:4 on ' % ('unsigned ' * bool(m[1])), local))
                return '' if 'Odd' in local else re.sub(r'(?<= )(20\d\d) (.*?):(\d+)', lambda m: f'{m[1]} {tail(m[2])}:{m[3]}', local)   # not where kinds.cpp's Odd is
            a, b = (described(s, lambda flags, name, link, site: (flags[0], int(flags[1]) & 0x80, tail(name), link, site and (site[0], as_rad(' ' + site[2].split(' of ')[0]))), as_rad, calls=False) for s in (ours, scopes(rad)))
            for k in a | b: q.append(f'scope, as RAD has it, {k}'); want.append(str(b[k])); got.append(str(a[k]))
            a, b = typed(rdi, 31, 'P'), typed(rad, 31, 'P')
            for k in a | b: q.append(f'procedure and its type {k}'); want.append(str(b[k])); got.append(str(a[k]))
            # every type, but for the built-in ones, their aliases and the pointers to those (RAD makes one for every basic
            # type code there could be, 3,199 of them pointers to no type; we make the ones used)
            # variables and constants; RAD also has a variable without type or place for each public symbol that is not code
            for table in (27, 29, 30):
                a, b = typed(rdi, table, f'G {table}'), typed(rad, table, f'G {table}')
                for k in a | b:
                    if k[1] != '0 0 0\tnone': q.append(f'variable, as RAD has it, {table} {k}'); want.append(str(b[k])); got.append(str(a[k]))
            # the variable at an offset, but not inside two that overlap without one holding the other: there RAD's map is
            # out of order and answers by luck (after the 16-byte table that _load_config_used starts inside, none in 2 of 14 files)
            crossed = [(a, a + n) for a, n, _ in vars_ for b, m, _ in vars_ if a < b < a + n < b + m or b < a < b + m < a + n]
            for v in points:
                if any(lo <= v < hi for lo, hi in crossed): continue
                a, b = typed(rdi, [v], 'O'), typed(rad, [v], 'O')
                q.append(f'O {v}, as RAD has it'); want.append(str(b)); got.append(str(a))
            a, b = typed(rdi, 20, 'M'), typed(rad, 20, 'M')
            for k in a | b: q.append(f'members, as RAD has them, {k[1]}'); want.append(str(b[k])); got.append(str(a[k]))
            a, b = typed(rdi, 19, 'T'), typed(rad, 19, 'T')
            for k in a | b:
                if not re.match(r'(1001\.0\[0\]:8 )?(none|\w+:\d+$|2004 [\w ]+:\d+ on \w+:\d+$)', k[1]): q.append(f'type {k[1]}'); want.append(str(b[k])); got.append(str(a[k]))
    bad = [(a, w, g) for a, w, g in zip(q, want, got) if w != g]
    if bad or len(got) != len(q):
        print(f'oracle_conv: {pdb}: {len(q)} questions, {len(got)} answers, {len(bad)} differ')
        for b in bad[:8]: print('  %s: expected [%s], our file says [%s]' % b)
        sys.exit(1)
    total += len(q)
print(f'oracle_conv: {total} answers from {len(sys.argv) - 3} converted files agree with llvm-pdbutil and, where recorded, with RAD\'s conversion')
sys.exit(0 if total >= 1000 else 1)
