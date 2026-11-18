#!/usr/bin/env python3
"""Review of PR 20: shrink a differing case.  Drops tokens (and then whole
files' lines) one at a time while the core still differs from live Neovim.
usage: reduce.py CASEFILE OUT"""
import os, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
rows = cols = 0; files = []; toks = []
for l in open(sys.argv[1], encoding='utf-8'):
    if l[0] == 'C': _, rows, cols = l.split()
    if l[0] == 'F': files.append(bytes.fromhex(l[2:].strip()).decode())
    if l[0] == 'T': toks.append(l[2:].rstrip('\n'))
def differs(files, toks, out=None):
    case = {'rows': int(rows), 'cols': int(cols), 'files': files, 'tokens': list(toks)}
    try: s = run_nvim(case)
    except Exception: return False
    case['screens'], case['marks'] = s['screens'], s['marks']
    for i in s['nop']: case['tokens'][i - 1] = 'nop'
    o = out or tempfile.mktemp(suffix='.txt')
    write_cases(o, [case])
    return subprocess.run([SCREENS, o], capture_output=True).returncode == 1
assert differs(files, toks)
toks = [t for t in toks if t != 'nop']
changed = True
while changed:
    changed = False
    for i in range(len(toks) - 1, -1, -1):
        t2 = toks[:i] + toks[i + 1:]
        if differs(files, t2): toks = t2; changed = True
for fi in range(len(files)):
    lines = files[fi].split('\n')
    i = len(lines) - 1
    while i >= 0:
        l2 = lines[:i] + lines[i + 1:]
        f2 = files[:fi] + ['\n'.join(l2)] + files[fi + 1:]
        if differs(f2, toks): lines = l2; files = f2
        i -= 1
differs(files, toks, sys.argv[2])
print(rows, cols, [f for f in files], ' '.join(toks))
