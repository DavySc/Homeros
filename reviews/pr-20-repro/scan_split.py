#!/usr/bin/env python3
"""Review of PR 20: N-line file, go to line L, CTRL-W s, CTRL-W p, for a grid of
N, L and screen rows; prints the combinations where the core's screen
differs from Neovim's.  usage: scan_split.py"""
import os, subprocess, sys, tempfile, concurrent.futures
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
jobs = []
for rows in (10, 15, 24, 30, 50):
    for n in (5, 12, 20, 23, 24, 30, 60, 200):
        for l in sorted(set(x for x in (1, 2, 5, 7, 8, 9, 12, 15, 18, 20, 23, 30, 45, 60, 150, n) if x <= n)):
            jobs.append((rows, n, l))
def run(j):
    rows, n, l = j
    case = {'rows': rows, 'cols': 40, 'files': [''.join('l%d\n' % i for i in range(1, n + 1))], 'tokens': ['k:%dG' % l, 'w:s', 'w:p']}
    s = run_nvim(case)
    case['screens'], case['marks'] = s['screens'], s['marks']
    return case
with concurrent.futures.ThreadPoolExecutor(4) as ex: cases = list(ex.map(run, jobs))
bad = 0
for j, c in zip(jobs, cases):
    o = tempfile.mktemp(suffix='.txt')
    write_cases(o, [c])
    p = subprocess.run([SCREENS, o], capture_output=True, text=True)
    if p.returncode:
        bad += 1
        print('rows %d, %d lines, line %d:' % j, ' / '.join(x.strip() for x in p.stderr.split('\n')[:3]))
print('%d of %d combinations differ' % (bad, len(jobs)))
