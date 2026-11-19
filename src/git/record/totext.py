#!/usr/bin/env python3
"""The recorded traces (record.py's JSON) as the text tests/test_git.c reads:
  @ name            a scenario       $ setup         its shell setup
  > keys            a step           w buffer|selected|row   a window (row -: no cursor)
  | text            the window's line   e echo        the echo area
  s key value       the repository's state, given when it changed
Values escape backslash, newline and tab.  Usage: totext.py IN.json OUT.txt"""
import json, sys

esc = lambda s: s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')
d = json.load(open(sys.argv[1]))
with open(sys.argv[2], 'w') as o:
    for name, sc in d.items():
        o.write('@ %s\n' % name)
        if sc['setup']: o.write('$ %s\n' % esc(sc['setup']))
        last = {}
        for st in sc['steps']:
            o.write('> %s\n' % esc(st['keys']))
            for w in st['windows']:
                o.write('w %s|%d|%s\n' % (w['buffer'], w['selected'], '-' if w['cursor'] is None else w['cursor'][0]))
                for l in w['lines']: o.write('| %s\n' % esc(l))
            o.write('e %s\n' % esc(st['echo']))
            for k in sorted(st['repo']):
                if last.get(k) != st['repo'][k]: o.write('s %s %s\n' % (k, esc(st['repo'][k])))
            last = st['repo']
