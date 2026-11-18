#!/usr/bin/env python3
"""Review of PR 20: the PR's trace generator with jumps to any line (NG),
plainer text and more window switching, so that cursors sit anywhere in a
window when it is split, resized or entered.  usage: gen2.py N SEED OUT"""
import os, concurrent.futures, random, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
gen_tokens0 = gen_tokens
def gen_tokens(r, nfiles):
    toks = []
    for t in gen_tokens0(r, nfiles):
        if not t.startswith('pum:') and r.random() < .25:
            toks.append('k:%dG' % r.choice([1, 5, 9, 12, 15, 18, 23, 30, 45, 60, 100]))
        if not t.startswith('pum:') and r.random() < .15:
            toks.append(r.choice(['w:p', 'w:w', 'w:k', 'w:j', 'w:t', 'w:b']))
        toks.append(t)
    return toks[:60]
n, seed, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
r = random.Random(seed)
cases = [gen_case(r) for _ in range(n)]
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for c, s in zip(cases, ex.map(run_nvim, cases)):
        c['screens'], c['marks'] = s['screens'], s['marks']
        for i in s['nop']: c['tokens'][i - 1] = 'nop'
write_cases(out, cases)
for b in (SCREENS, os.environ.get('SCREENS2', SCREENS)):
    p = subprocess.run([b, out], capture_output=True, text=True)
    print(b + ':', p.stdout.strip())
    print(p.stderr[-3000:])
