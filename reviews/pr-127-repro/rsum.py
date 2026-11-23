# rsum.py file...: of the lines "walk up", the ratio up / walk: count, min, median, max, and how many are on the wrong side of 1
import sys, statistics
for f in sys.argv[1:]:
    r = sorted(int(b) / int(a) for a, b in (l.split() for l in open(f) if l.strip()))
    print('%-28s n=%d min %.3f p1 %.3f median %.3f p99 %.3f max %.3f  below 1: %d  at or above 1: %d' % (f, len(r), r[0], r[len(r) // 100], statistics.median(r), r[-1 - len(r) // 100], r[-1], sum(x < 1 for x in r), sum(x >= 1 for x in r)))
