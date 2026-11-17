#!/usr/bin/env python3
"""Review of PR 20: cut case N out of a recorded trace file.
usage: onecase.py FILE N OUT [--show] ; --show prints size, files and tokens"""
import os, sys
f, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
cases, cur = [], []
for line in open(f, encoding='utf-8'):
    cur.append(line)
    if line.startswith('E'): cases.append(cur); cur = []
c = cases[n]
open(out, 'w', encoding='utf-8').write(''.join(c))
if '--show' in sys.argv:
    for l in c:
        if l[0] == 'C': print(l.strip())
        if l[0] == 'F': print('F', repr(bytes.fromhex(l[2:].strip()).decode()))
        if l[0] == 'T': print(l.strip())
