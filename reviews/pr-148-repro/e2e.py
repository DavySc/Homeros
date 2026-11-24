#!/usr/bin/env python3
"""Reads e2e's output beside the file: every run of bytes where the face
drawn is not the grammar's, with both faces' names.  e2e.py FILE OUT grid.h"""
import re, sys
src = open(sys.argv[1], 'rb').read()
rows = [tuple(map(int, l.split())) for l in open(sys.argv[2])]
enum = re.search(r'enum \{\s*FACE_NONE.*?\}', open(sys.argv[3]).read(), re.S).group(0)
names = re.findall(r'FACE_(\w+)', enum)
i = 0; seen = {}
while i < len(rows):
    j = i
    while j < len(rows) and rows[j] == rows[i]: j += 1
    g, f = rows[i]
    if g != f:
        k = (names[g], names[f]); seen.setdefault(k, []).append(src[i:j].decode('utf-8', 'replace'))
    i = j
for (g, f), w in sorted(seen.items()):
    print(f'grammar {g:22} drawn {f:10} x{len(w):3}: {" ".join(dict.fromkeys(w))[:150]}')
