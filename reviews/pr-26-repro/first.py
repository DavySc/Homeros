#!/usr/bin/env python3
"""For each differing case of a gen2.py result file: the shortest prefix of
its keys that already differs (binary search, then checked), printed with
the last 8 keys of that prefix.  usage: first.py DRIVER FILE.json"""
import json, sys, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/conform.py')).read()
G = {}
if os.environ.get('NOMATCHIT'): src = src.replace("'--clean', '--headless',", "'--clean', '--headless', '--cmd', 'let g:loaded_matchit=1',")
exec(src[:src.rindex('main()')].replace('timeout=600', 'timeout=20'), G)
def differs(text, keys):
    c = {'text': text, 'keys': keys}
    try:
        w = G['nvim1']([c])[0]; g = G['ours'](sys.argv[1], c)
    except Exception: return None, None, None
    return (g.get('lines') != w['lines'] or g.get('cursor') != w['cursor']), w, g
for c in json.load(open(sys.argv[2])):
    if 'error' in c['ours']: continue
    lo, hi = 1, len(c['keys'])          # invariant: prefix hi differs
    while lo < hi:
        mid = (lo + hi) // 2
        if differs(c['text'], c['keys'][:mid])[0]: hi = mid
        else: lo = mid + 1
    d, w, g = differs(c['text'], c['keys'][:hi])
    print('%s | text %s | ...%s | nvim %s %s | ours %s %s' % ('ok ' if d else '?? ', json.dumps(c['text'], ensure_ascii=False), ' '.join(c['keys'][max(0, hi - 8):hi]),
          w and w['cursor'], '' if not w or w['lines'] == g.get('lines') else w['lines'], g and g.get('cursor'), '' if not w or w['lines'] == g.get('lines') else g.get('lines')), flush=True)
