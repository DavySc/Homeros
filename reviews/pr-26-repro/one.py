#!/usr/bin/env python3
"""One case in Neovim and in the driver.  usage: one.py DRIVER TEXT 'k1 k2 ...'  (keys split on spaces; <Space> for a space)"""
import json, sys, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/conform.py')).read()
G = {}
if os.environ.get('NOMATCHIT'): src = src.replace("'--clean', '--headless',", "'--clean', '--headless', '--cmd', 'let g:loaded_matchit=1',")   # Neovim's built-in %
exec(src[:src.rindex('main()')], G)
G['CODES'].update({'<C-i>': 9, '<C-e>': 5, '<C-y>': 25, '<C-f>': 6, '<C-b>': 2, '<C-l>': 12, '<C-g>': 7, '<C-k>': 11,
                   '<C-n>': 14, '<C-h>': 8, '<C-j>': 10, '<C-m>': 13, '<C-c>': 3})
if len(sys.argv) > 4: G['SETUP'] += sys.argv[4]
text = sys.argv[2].encode().decode('unicode_escape').encode('latin1').decode() if '\\' in sys.argv[2] else sys.argv[2]
keys = [' ' if k == '<Space>' else k for k in sys.argv[3].split(' ')]
c = {'text': text, 'keys': keys}
w, g = G['nvim1']([c])[0], G['ours'](sys.argv[1], c)
same = g.get('lines') == w['lines'] and g.get('cursor') == w['cursor']
print('SAME' if same else 'DIFF', json.dumps(text, ensure_ascii=False), ' '.join(sys.argv[3].split(' ')))
if not same: print('  nvim', w['lines'], w['cursor'], '\n  ours', g.get('lines'), g.get('cursor') or g)
