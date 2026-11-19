#!/usr/bin/env python3
"""Review of PR 56: compares two files recorded by rec.py (Neovim's and the
editor's own terminal output), case by case by name: every row's text,
backgrounds and heading colours but the status line's, and the cursor.
usage: cmprec.py NVIM.txt HOMEROS.txt"""
import sys, unicodedata
def load(p):
    d, cur = {}, None
    for l in open(p, encoding='utf-8'):
        l = l.rstrip('\n')
        if l[:2] == 'C ': cur = d.setdefault(l.split(' ', 3)[3], {'rows': [], 'x': None, 'cmd': False})
        elif l[:2] == 'X ': cur['x'] = l[2:]
        elif l[:2] == 'T ': cur['cmd'] = cur['cmd'] or l.endswith(':')
        elif l[:2] in ('S ', 'B ', 'F '): cur['rows'].append(unicodedata.normalize('NFC', l))
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for name in b:
    ra, rb = a[name]['rows'][:-3], b[name]['rows'][:-3]   # not the status line
    diff = [(i, x, y) for i, (x, y) in enumerate(zip(ra, rb)) if x != y]
    if diff:
        bad += 1
        i, x, y = diff[0]
        print('[%s] row %d:\n  nvim    [%s]\n  homeros [%s]' % (name, i // 3, x, y))
    elif a[name]['x'] != b[name]['x'] and not a[name]['cmd']:
        bad += 1
        print('[%s] cursor: nvim %s homeros %s' % (name, a[name]['x'], b[name]['x']))
print('cmprec: %d cases, %d differ' % (len(b), bad))
sys.exit(bad != 0)
