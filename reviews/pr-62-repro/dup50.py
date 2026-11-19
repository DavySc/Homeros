#!/usr/bin/env python3
"""Runs of N identical tokens (default 50) occurring twice in the given C files (PMD CPD style, comments dropped)."""
import re, sys
N = 50
files = sys.argv[1:]
toks = []
for f in files:
    s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group().count('\n'), open(f).read(), flags=re.S)
    for ln, line in enumerate(s.split('\n'), 1):
        if line.lstrip().startswith('#'): continue
        for t in re.findall(r'[A-Za-z_]\w*|\d[\w.]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|->|[<>=!+\-*/|&]=|&&|\|\||\S', line):
            toks.append((t, f, ln))
seen, hits, i = {}, [], 0
keys = [tuple(t[0] for t in toks[k:k + N]) for k in range(len(toks) - N + 1)]
last = -N
for k, key in enumerate(keys):
    if key in seen and k - seen[key] >= N:
        if k - last >= N: hits.append((toks[seen[key]][1:], toks[k][1:]))
        last = k
    else: seen.setdefault(key, k)
for a, b in hits: print('%s:%d == %s:%d' % (a + b))
print(len(hits), 'duplicate runs of', N, 'tokens')
