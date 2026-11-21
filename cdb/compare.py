#!/usr/bin/env python3
"""Where RAD's and cdb's recordings of the same program part: compare.py <traces dir> <traces-cdb dir>

One line per recording that both have (into, over): the step at which the two are at different addresses, and where
each is. A recording RAD ended (the program left its own code or ended) while cdb stepped on agrees to its end.
"""
import re, sys
from pathlib import Path

def rad(p):
    out = []
    for l in p.read_text().splitlines():
        m = re.search(r'-> (\S+) \S*:(\d*) \+0x([0-9a-f]+)', l)
        out.append((int(m[3], 16), f'{m[1]}:{m[2]}') if m else (None, l.split('->')[1].strip()[:30]))
    return out
def cdb(p):
    return [(int(m[2].replace('`', ''), 16) - 0x140000000, m[1].split('!')[-1])
            for m in re.finditer(r'STEP \w+ (\S+?)(?:\+0x[0-9a-f]+)? \(([0-9a-f`]+)\)', p.read_text(encoding='utf-8-sig', errors='replace'))]
agree = 0
for p in sorted(Path(sys.argv[2]).glob('*.txt')):
    r = Path(sys.argv[1]) / p.name
    if not r.exists(): continue
    a, b = rad(r), cdb(p)
    k = next((i for i in range(min(len(a), len(b))) if a[i][0] != b[i][0]), min(len(a), len(b)))
    if k == len(a) or (k == len(a) - 1 and a[k][0] is None): agree += 1; continue   # RAD's last line: ended, or outside the program
    print(f'{p.name:24} step {k:3}: after {a[k - 1][1]:28} RAD {a[k][1]} +{a[k][0]:#x}, cdb {b[k][1]} +{b[k][0]:#x}' if k < len(b) and a[k][0] is not None else f'{p.name:24} step {k:3}: {a[k][1]} | {b[k] if k < len(b) else "cdb has no more"}')
print(f'{agree} recordings agree to RAD\'s end')
