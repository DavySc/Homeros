#!/usr/bin/env python3
# Review of package 9, note 5.  Not a measurement of Windows: arithmetic on
# the same picture Pace.tla uses (DWM composes every P, a frame is seen at
# the first composition after it is drawn), with D = 0.  2,000 keys 1 ms
# apart, as the design's prototype test (DESIGN.md, "When to draw").
#   gap    the PR: frames at least P + P/8 apart
#   none   draw on every key
import math, statistics
P = 1000 / 60
keys = [0.37 + i for i in range(2000)]

def run(rule):
    draws, last = [], -1e9
    for t in keys:                       # a frame shows every key typed up to its time
        if draws and draws[-1] >= t: continue        # a frame is already due that will show this key
        if rule == 'gap': d = max(t, last + P + P / 8)
        else: d = t
        draws.append(d); last = d
    wasted = sum(math.floor(a / P) == math.floor(b / P) for a, b in zip(draws, draws[1:]))
    l, i = [], 0
    for t in keys:
        while draws[i] < t: i += 1
        l.append((math.floor(draws[i] / P) + 1) * P - t)   # seen at the first composition after its frame
    l.sort()
    comps = math.floor(keys[-1] / P) + 1
    fresh = len({math.floor(d / P) for d in draws})
    return len(draws), wasted, statistics.median(l), l[int(len(l) * .99)], l[-1], comps - fresh

for r in ('gap', 'none'):
    f, w, p50, p99, mx, stale = run(r)
    print(f'{r:5}  frames {f:4}  never shown {w:4}  compositions with nothing new {max(stale, 0):3} of 120  '
          f'key to screen ms: median {p50:4.1f}  p99 {p99:4.1f}  max {mx:4.1f}')
