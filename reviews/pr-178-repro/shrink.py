#!/usr/bin/env python3
"""shrink.py FILE: drop lines, then characters, while Org and ours still differ (asked at every character)"""
import subprocess, sys, os
src = open(sys.argv[1], 'rb').read()
T = '/tmp/claude-0/shr.org'
def differ(b):
    open(T, 'wb').write(b)
    open(T + '.cmd', 'w').write(str(len(b)) if os.environ.get('POS') == 'end' else ' '.join(str(j) for j in range(len(b) + 1) if j == len(b) or (b[j] & 0xc0) != 0x80))
    a = subprocess.run(['emacs', '-Q', '--batch', '-l', 'tools/org_open.el', T], capture_output=True, stdin=subprocess.DEVNULL).stdout
    c = subprocess.run(['rv/drv_san', '-o', T], capture_output=True).stdout
    if a == c: return False
    al, cl = a.split(b'\n'), c.split(b'\n')
    want = sys.argv[2].encode() if len(sys.argv) > 2 else None
    return any(x != y and (not want or want in x or want in y) for x, y in zip(al, cl))
assert differ(src)
ls = src.split(b'\n')
i = 0
while i < len(ls):
    t = ls[:i] + ls[i + 1:]
    if differ(b'\n'.join(t)): ls = t
    else: i += 1
b = b'\n'.join(ls); i = 0
while i < len(b):
    t = b[:i] + b[i + 1:]
    try: t.decode()
    except UnicodeDecodeError: i += 1; continue
    if differ(t): b = t
    else: i += 1
differ(b)
sys.stdout.buffer.write(b + b'\n--\n')
