#!/usr/bin/env python3
"""Wall time of both builds: 5 alternating runs per side, median and range.
timing.py FILE...   (appends every run to timing.log and prints a table)"""
import subprocess, sys, statistics as st, re
def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout
def stat(v): return '%.3g (%.3g to %.3g)' % (st.median(v), min(v), max(v))
log = open('timing.log', 'a')
for f in sys.argv[1:]:
    full = {'a': [], 'b': []}
    ed = {k: {x: {'med': [], 'p99': [], 'max': []} for x in ('a', 'b', 'bs')} for k in 'rt'}
    for i in range(5):
        for x in ('a', 'b'):
            o = run(['./bench_' + x, 'full', f, '21']); log.write('%s %s %s' % (f, x, o))
            full[x].append(float(o.split()[-2]))
        for k in 'rt':
            for x in ('a', 'b', 'bs'):
                o = run(['./bench_' + x, 'edits', f, '2000', k]); log.write('%s %s %s' % (f, x, o))
                m = re.search(r'median ([\d.]+) us p99 ([\d.]+) us max ([\d.]+) us', o)
                for j, n in enumerate(('med', 'p99', 'max')): ed[k][x][n].append(float(m.group(j + 1)))
    print('##', f)
    print('full parse, ms: A %s | B %s' % (stat(full['a']), stat(full['b'])))
    for k, name in (('r', 'random one-byte inserts'), ('t', 'typing runs of 50')):
        for n in ('med', 'p99', 'max'):
            print('%s, %s, us: A %s | B whole %s | B subtree only %s' % (name, n, stat(ed[k]['a'][n]), stat(ed[k]['b'][n]), stat(ed[k]['bs'][n])))
