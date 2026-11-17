# usage: cmp.py el.json other.json...   files (of those in el.json) where each count agrees with Org's parser
import json, sys
el = json.load(open(sys.argv[1]))
for name in sys.argv[2:]:
    o = json.load(open(name)); print(name)
    for k in ['headlines', 'src_blocks', 'example_blocks', 'tables', 'property_drawers', 'plain_lists', 'timestamps']:
        bad = [(f, el[f].get(k, 0), o.get(f, {}).get(k, 0)) for f in el if el[f].get(k, 0) != o.get(f, {}).get(k, 0)]
        print('  %-17s %2d of %d agree' % (k, len(el) - len(bad), len(el)), bad if len(bad) < 4 else '')
