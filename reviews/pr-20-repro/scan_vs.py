#!/usr/bin/env python3
"""Review of PR 20: wrap, go to line L, :vs (or :sp), for a grid of screen
sizes; prints where the core's screen differs from Neovim's, with and
without the review's two experiments.  usage: scan_vs.py"""
import os, subprocess, sys, tempfile, concurrent.futures
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
text = ', ა\n\t( longerword\n\n\t\n\tλx foo ).\n    \t  ( , -- foo \t longerword aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa --\n(\n' * 4
jobs = [(rows, cols, l, cmd) for rows in (10, 20, 30) for cols in (20, 30, 40, 41, 60, 80) for l in (1, 7, 14, 28) for cmd in ('vs', 'sp', 'w:v', 'w7:v')]
def run(j):
    rows, cols, l, cmd = j
    case = {'rows': rows, 'cols': cols, 'files': [text], 'tokens': ['wrap', 'k:%dG' % l, cmd, 'w:p', 'w:p']}
    s = run_nvim(case)
    case['screens'], case['marks'] = s['screens'], s['marks']
    return case
with concurrent.futures.ThreadPoolExecutor(4) as ex: cases = list(ex.map(run, jobs))
bad = [0, 0]
for j, c in zip(jobs, cases):
    o = tempfile.mktemp(suffix='.txt')
    write_cases(o, [c])
    r = [subprocess.run([b, o], capture_output=True, text=True) for b in (SCREENS, os.environ.get('SCREENS2', SCREENS))]
    for i in (0, 1): bad[i] += r[i].returncode != 0
    if r[1].returncode:
        print('rows %d cols %d line %d %s:' % j, ' / '.join(x.strip() for x in r[1].stderr.split('\n')[:3]))
print('%d of %d differ as the PR stands, %d with the two experiments' % (bad[0], len(jobs), bad[1]))
