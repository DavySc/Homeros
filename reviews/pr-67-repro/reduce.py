#!/usr/bin/env python3
"""reduce.py REPO DRIVER FILE.json: each differing case of gq.py made small
(keys, then lines, then words dropped while Neovim and ours still differ)."""
import json, sys, os, re
repo, drv = sys.argv[1:3]
src = open(os.path.join(repo, 'tools/conform.py')).read()
G = {'__file__': os.path.join(repo, 'tools/conform.py')}
exec(src[:src.rindex('main()')], G)
def differs(c):
    g = G['ours'](drv, c)
    return 'error' in g or not G['same'](g, G['nvim1']([c])[0])
def shrink(items, make):
    i = 0
    while i < len(items):
        t = items[:i] + items[i + 1:]
        if differs(make(t)): items = t
        else: i += 1
    return items
for b in json.load(open(sys.argv[3])):
    keys = [t for k in b['keys'] for t in ([k] if k in ('gq', 'go', 'gg', 'ap', 'ip') else re.findall('<[^<>]+>|.', k))]
    text = b['text']
    if not differs({'text': text, 'keys': keys}): keys = b['keys']
    for _ in range(2):
        keys = shrink(keys, lambda t: {'text': text, 'keys': t})
        text = '\n'.join(shrink(text.split('\n'), lambda t: {'text': '\n'.join(t), 'keys': keys}))
        text = ' '.join(shrink(text.split(' '), lambda t: {'text': ' '.join(t), 'keys': keys}))
    c = {'text': text, 'keys': keys}
    print(json.dumps(c, ensure_ascii=False), '\n nvim', G['nvim1']([c])[0], '\n ours', G['ours'](drv, c), flush=True)
