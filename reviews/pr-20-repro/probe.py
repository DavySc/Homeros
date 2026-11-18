#!/usr/bin/env python3
"""Review of PR 20: replay the first K tokens of a one-case trace file in
nvim --headless exactly as tests/test_core_screens.py does (a 'redraw!' and
a screen read after every token), then run a Lua chunk and print what it
returns.  usage: probe.py CASEFILE K LUAFILE [extra token ...]"""
import json, os, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
case, k, lua = sys.argv[1], int(sys.argv[2]), open(sys.argv[3]).read()
rows = cols = 0; files = []; toks = []
for l in open(case, encoding='utf-8'):
    if l[0] == 'C': _, rows, cols = l.split()
    if l[0] == 'F': files.append(bytes.fromhex(l[2:].strip()).decode())
    if l[0] == 'T': toks.append(l[2:].rstrip('\n'))
toks = toks[:k] + sys.argv[4:]
d = tempfile.mkdtemp()
for i, t in enumerate(files): open(os.path.join(d, 'f%d' % i), 'w').write(t)
fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
json.dump({'rows': int(rows), 'cols': int(cols), 'keys': [x if isinstance(x, list) else [x] for x in map(nvim_keys, toks)]}, open(fi, 'w'))
body = LUA.replace("io.open(OUT, 'w'):write(vim.json.encode({screens = out, marks = mk, nop = nop}))",
                   "local function PROBE()\n" + lua + "\nend\nio.open(OUT, 'w'):write(vim.json.encode({screens = out, probe = PROBE()}))")
open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + body)
subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + fl, 'f0'], cwd=d, stdin=subprocess.DEVNULL, timeout=60)
o = json.load(open(fo))
print('tokens:', toks)
for r in o['screens'][-1]: print('|' + r.rstrip() + '|')
print('probe:', json.dumps(o.get('probe'), ensure_ascii=False))
