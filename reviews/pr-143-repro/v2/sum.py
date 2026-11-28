# sum.py <file>...: per file of probe runs, for every check: min / median / max of its ratio against its bound
import sys, statistics as st
LB = 16; GB = [2, 2.7, 20, 20, 3.2, 8, 40, 20]
def bound(k): return {'L': LB, 'S': 32, 'R': 0.5}.get(k[0]) or GB[k[1]]
for f in sys.argv[1:]:
    d = {}
    for ln in open(f, errors='replace'):
        w = ln.split()
        if len(w) == 5 and w[0] in ('L', 'S', 'G', 'R'):
            b = float(w[4]); d.setdefault((w[0], int(w[1])), []).append(float(w[3]) / b if b > 0 else float('inf'))
    print(f)
    for k in sorted(d, key=lambda k: ('LSGR'.index(k[0]), k[1])):
        v = d[k]; b = bound(k)
        print('  %s%-2d n=%-4d min %8.2f med %8.2f max %8.2f  bound %-4g max/bound %.2f min/bound %.2f%s' % (k[0], k[1], len(v), min(v), st.median(v), max(v), b, max(v) / b, min(v) / b, ' OVER %d' % sum(x >= b for x in v) if max(v) >= b else ''))
