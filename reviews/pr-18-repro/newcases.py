#!/usr/bin/env python3
"""The cases PR 18 adds to tests/conform-recorded.json: are the recorded
Neovim results what live Neovim gives, and does a driver pass them?
usage: newcases.py OLD.json NEW.json DRIVER [DRIVER...]
  OLD.json: git show origin/main:tests/conform-recorded.json
Run from the repo root (uses tools/conform.py's nvim() and ours())."""
import json, sys, types
# conform.py runs main() when imported: take its definitions only
conform = types.ModuleType('conform')
exec(open('tools/conform.py').read().rsplit('\nmain()', 1)[0], conform.__dict__)

old, new = (json.load(open(p)) for p in sys.argv[1:3])
same = lambda g, w: g.get('lines') == w['lines'] and g.get('cursor') == w['cursor']   # as conform.py compares
key = lambda c: json.dumps(c, sort_keys=True)
seen = {key(c) for c in old}
print('%d old cases, %d of them missing or changed in the new file' % (len(old), len(seen - {key(c) for c in new})))
added = [c for c in new if key(c) not in seen]
live = conform.nvim([{'text': c['text'], 'keys': c['keys']} for c in added])
print('%d added; recorded result differs from live nvim in %d' % (len(added), sum(not same(l, c['nvim']) for l, c in zip(live, added))))
for drv in sys.argv[3:]:
    bad = [c for c in added if not same(conform.ours(drv, c), c['nvim'])]
    print('%s: fails %d of %d' % (drv, len(bad), len(added)))
    for c in bad: print('   ', json.dumps(c['keys'], ensure_ascii=False), 'on', json.dumps(c['text'], ensure_ascii=False))
