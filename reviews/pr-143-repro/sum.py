# sum.py <file>...: each file holds probe runs (NOFAIL, one try a check), runs split by the "test_org:" line or a "RUN" line
# per file: for every check, min / median / max of org time over plain time, against its bound
import sys, statistics as st
LB = 16; GB = [3, 3.5, 20, 20, 4, 10, 40, 20]
def bound(k): return LB if k[0] == 'L' else 0.5 if k[0] == 'R' else GB[k[1]]
for f in sys.argv[1:]:
    d = {}
    for ln in open(f, errors='replace'):
        w = ln.split()
        if len(w) == 5 and w[0] in ('L', 'G', 'R'):
            b = float(w[4])
            d.setdefault((w[0], int(w[1])), []).append(float(w[3]) / b if b > 0 else float('inf'))
    print(f)
    for k in sorted(d, key=lambda k: ('LGR'.index(k[0]), k[1])):
        v = d[k]; b = bound(k)
        flag = ' OVER %d of %d' % (sum(x >= b for x in v), len(v)) if max(v) >= b else ''
        print('  %s%-2d n=%-4d min %8.2f med %8.2f max %8.2f  bound %-4g max/bound %.2f min/bound %.2f%s' % (k[0], k[1], len(v), min(v), st.median(v), max(v), b, max(v) / b, min(v) / b, flag))
