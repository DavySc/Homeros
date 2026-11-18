#!/usr/bin/env python3
"""Cut each differing case in a gen2.py result file down to the shortest key
list that still differs (dropping one command token at a time), then print
the reduced cases grouped by their last key.
usage: reduce.py DRIVER FILE.json [setup-extra]"""
import json, sys, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/conform.py')).read()
G = {}
if os.environ.get('NOMATCHIT'): src = src.replace("'--clean', '--headless',", "'--clean', '--headless', '--cmd', 'let g:loaded_matchit=1',")   # Neovim's built-in %
# a cut-down case can loop in Neovim (a macro running itself): 15 s, then it counts as not differing
exec(src[:src.rindex('main()')].replace('timeout=600', 'timeout=15'), G)
drv = sys.argv[1]
if len(sys.argv) > 3: G['SETUP'] += sys.argv[3]
G['CODES'].update({'<C-i>': 9, '<C-e>': 5, '<C-y>': 25, '<C-f>': 6, '<C-b>': 2, '<C-l>': 12, '<C-g>': 7, '<C-k>': 11,
                   '<C-n>': 14, '<C-h>': 8, '<C-j>': 10, '<C-m>': 13, '<C-c>': 3})
def differs(text, keys):
    c = {'text': text, 'keys': keys}
    try:
        w = G['nvim1']([c])[0]
        g = G['ours'](drv, c)
    except Exception:
        return False, None, None
    return (g.get('lines') != w['lines'] or g.get('cursor') != w['cursor']), w, g
out = []
only = [int(x) for x in os.environ['ONLY'].split(',')] if os.environ.get('ONLY') else None
for i, c in enumerate(json.load(open(sys.argv[2], errors='surrogateescape'))):
    if only and i not in only: continue
    text, keys = c['text'], c['keys']
    d, w, g = differs(text, keys)
    if not d: print('FLAKY', c['keys'], flush=True); continue
    lo, hi = 1, len(keys)   # first the shortest differing prefix (binary search)
    while lo < hi:
        mid = (lo + hi) // 2
        if differs(text, keys[:mid])[0]: hi = mid
        else: lo = mid + 1
    if differs(text, keys[:hi])[0]: keys = keys[:hi]
    changed = True
    while changed:
        changed = False
        for i in range(len(keys) - 1, -1, -1):
            k2 = keys[:i] + keys[i + 1:]
            if k2 and differs(text, k2)[0]: keys, changed = k2, True
        lines = text.split('\n')
        for i in range(len(lines) - 1, -1, -1):
            t2 = '\n'.join(lines[:i] + lines[i + 1:])
            if differs(t2, keys)[0]: lines, text, changed = lines[:i] + lines[i + 1:], t2, True
    d, w, g = differs(text, keys)
    print(' '.join(keys), '|', json.dumps(text, ensure_ascii=False), '| nvim', w, '| ours', g, flush=True)
