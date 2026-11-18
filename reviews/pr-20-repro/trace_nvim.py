#!/usr/bin/env python3
"""Review of PR 20: Neovim's cursor, curswant and view after every token of
a one-case trace file (the harness of tests/test_core_screens.py, with a
winsaveview() after each token's screen read).  usage: trace_nvim.py CASEFILE"""
import json, os, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))   # the repo
SCREENS = os.environ.get('SCREENS', ROOT + '/build/san/test_core_screens')   # make test TESTS=core_screens builds it
src = open(ROOT + '/tests/test_core_screens.py').read().split('\ndef main')[0]
exec(src)
rows = cols = 0; files = []; toks = []
for l in open(sys.argv[1], encoding='utf-8'):
    if l[0] == 'C': _, rows, cols = l.split()
    if l[0] == 'F': files.append(bytes.fromhex(l[2:].strip()).decode())
    if l[0] == 'T': toks.append(l[2:].rstrip('\n'))
d = tempfile.mkdtemp()
for i, t in enumerate(files): open(os.path.join(d, 'f%d' % i), 'w').write(t)
fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
json.dump({'rows': int(rows), 'cols': int(cols), 'keys': [x if isinstance(x, list) else [x] for x in map(nvim_keys, toks)]}, open(fi, 'w'))
body = LUA.replace("  mk[#mk + 1] = cmarks or marks()", "  mk[#mk + 1] = cmarks or marks()\n  VW = VW or {}; local v = vim.fn.winsaveview(); v.wrap = vim.wo.wrap; v.h = vim.fn.winheight(0); v.w = vim.fn.winwidth(0); VW[#VW + 1] = v")
body = body.replace("{screens = out, marks = mk, nop = nop}", "{screens = out, views = VW}")
open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + body)
subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + fl, 'f0'], cwd=d, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
o = json.load(open(fo))
for t, v in zip(toks, o['views']):
    print('N %-10s line %d col %d want %d top %d skip %d left %d wrap %d h %d w %d' % (t[:10], v['lnum'], v['col'], v['curswant'], v['topline'], v['skipcol'], v['leftcol'], v['wrap'], v['h'], v['w']))
