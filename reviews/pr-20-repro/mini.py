#!/usr/bin/env python3
"""Review of PR 20: one hand-written case against Neovim and the core.
usage: mini.py ROWS COLS FILE0[,FILE1..] token ...   (files are Python string
literals, or @path to read a file; -q prints only the verdict)
Records Neovim's screens as tests/test_core_screens.py does and replays them
in tests/test_core_screens.c (built as ./screens)."""
import ast, os, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
a = [x for x in sys.argv[1:] if x != '-q']
def lit(x): return open(x[1:], encoding='utf-8').read() if x.startswith('@') else ast.literal_eval("'''" + x + "'''")
case = {'rows': int(a[0]), 'cols': int(a[1]), 'files': [lit(x) for x in a[2].split(',')], 'tokens': a[3:]}
s = run_nvim(case)
case['screens'], case['marks'] = s['screens'], s['marks']
for i in s['nop']: case['tokens'][i - 1] = 'nop'
out = os.environ.get('OUT') or tempfile.mktemp(suffix='.txt')
write_cases(out, [case])
p = subprocess.run([SCREENS] + ([] if '-q' in sys.argv else ['-v']) + [out], capture_output=True, text=True)
print(p.stdout.strip()); print(p.stderr[:6000])
