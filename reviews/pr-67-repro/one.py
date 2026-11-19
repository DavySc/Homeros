#!/usr/bin/env python3
"""one.py REPO DRIVER TEXT KEYS...: one case in Neovim and in ours (keys as conform.py tokens)."""
import sys, os, json
repo, drv = sys.argv[1:3]
src = open(os.path.join(repo, 'tools/conform.py')).read()
G = {'__file__': os.path.join(repo, 'tools/conform.py')}
exec(src[:src.rindex('main()')], G)
c = {'text': sys.argv[3].encode().decode('unicode_escape').encode('latin1').decode() if '\\' in sys.argv[3] else sys.argv[3], 'keys': sys.argv[4:]}
w, g = G['nvim']([c])[0], G['ours'](drv, c)
print('same' if G['same'](g, w) else 'DIFFER', '\n nvim', w['cursor'], w['lines'], '\n ours', g.get('cursor'), g.get('lines', g))
