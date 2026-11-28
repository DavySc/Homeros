#!/usr/bin/env python3
"""PR 165 review: case files for fake.c.  usage: mkcases.py defs|diag DIR [LINES NDIAG]"""
import json, os, sys
kind, d = sys.argv[1], sys.argv[2]
os.makedirs(d + '/0', exist_ok=True)
open(d + '/lsproot', 'w').close()
if kind == 'defs':
    files = {'m.c': 'int a;\nint b;\nint c;\nint d;\n', 'z.h': 'int z;\n', 'o.c': ''}
    R = lambda n, l, c: {'uri': 'ROOT/' + n, 'range': {'start': {'line': l, 'character': c}, 'end': {'line': l, 'character': c + 1}}}
    which = sys.argv[3]
    res = [R('m.c', 3, 4), R('m.c', 1, 4)] if which == 'same' else [R('z.h', 0, 4), R('m.c', 2, 4)]
    c = {'k': 0, 'kind': 'loc', 'files': files, 'method': 'textDocument/definition', 'result': res}
else:
    lines, nd = int(sys.argv[3]), int(sys.argv[4])
    files = {'m.c': 'int x;\n' * lines}
    c = {'k': 0, 'kind': 'diag', 'files': files, 'diags': [[7 * i + 4, 7 * i + 5] for i in range(nd)]}
for n, t in files.items(): open(d + '/0/' + n, 'w').write(t)
json.dump(c, open(d + '/cases.txt', 'w')); open(d + '/cases.txt', 'a').write('\n')
