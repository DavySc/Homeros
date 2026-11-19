#!/usr/bin/env python3
"""Compare two trace files case by case: how many cases and screens differ."""
import sys
def load(p):
    out = []
    for blk in open(p, encoding='utf-8').read().split('E\n'):
        if not blk.strip(): continue
        head, *scr = blk.split('P\n')
        # screens are stored as differences from the one before: expand
        full, prev = [], []
        for s in scr:
            rows = s.split('\n')[:-1]
            cur = [prev[i] if r == 'R' else r for i, r in enumerate(rows)]
            full.append(cur); prev = cur
        out.append((head, full))
    return out
a, b = load(sys.argv[1]), load(sys.argv[2])
assert len(a) == len(b), (len(a), len(b))
dc = ds = heads = 0
ids = []
for i, ((ha, sa), (hb, sb)) in enumerate(zip(a, b)):
    if ha != hb: heads += 1
    n = sum(x != y for x, y in zip(sa, sb)) + abs(len(sa) - len(sb))
    if n: dc += 1; ds += n; ids.append(i)
print('cases', len(a), 'screens', sum(len(s) for _, s in a), 'heads differ', heads, 'cases differ', dc, 'screens differ', ds)
if '-l' in sys.argv: print(ids)
