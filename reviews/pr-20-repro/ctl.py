#!/usr/bin/env python3
"""Review of PR 20: the PR's trace generator with other text: double-width
characters, control characters, lines past 512 bytes, more lines.
usage: wide.py N SEED OUT"""
import os, concurrent.futures, random, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
WORDS2 = ['foo', 'x', '(', 'é', 'λx', '\x01', '\x7f', '\x1b', 'a' * 30, '\t', '', 'z' * 300, 'wörd ' * 120]
def gen_text(r):
    lines = []
    for _ in range(r.choice([1, 3, 8, 20, 40, 120])):
        ind = r.choice(['', '', ' ', '\t'])
        lines.append(ind + ' '.join(r.choice(WORDS2) for _ in range(r.randint(0, r.choice([3, 12])))))
    while sum(len(l.encode()) + 1 for l in lines) > 30000: lines.pop(r.randrange(len(lines)))   # the replayer reads a file from one 64 KiB line of hex
    return '\n'.join(lines) + '\n'
n, seed, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
r = random.Random(seed)
cases = [gen_case(r) for _ in range(n)]
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for c, s in zip(cases, ex.map(run_nvim, cases)):
        c['screens'], c['marks'] = s['screens'], s['marks']
        for i in s['nop']: c['tokens'][i - 1] = 'nop'
write_cases(out, cases)
sys.exit(subprocess.run([SCREENS, out]).returncode)
