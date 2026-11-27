# plantsum.py <tag> <plant>...: for each plant's probe runs out/<tag>-<plant>.txt, the check where it is furthest over its
# bound, and the least it was over there (ratio / bound; under 1 on a single try means a try that passes)
import sys, statistics as st
LB = 16; GB = [3, 3.5, 20, 20, 4, 10, 40, 20]
def bound(k): return LB if k[0] == 'L' else 0.5 if k[0] == 'R' else GB[k[1]]
tag = sys.argv[1]
for p in sys.argv[2:]:
    d = {}
    for ln in open('out/%s-%s.txt' % (tag, p), errors='replace'):
        w = ln.split()
        if len(w) == 5 and w[0] in ('L', 'G', 'R'):
            k = (w[0], int(w[1])); b = float(w[4])
            d.setdefault(k, []).append((float(w[3]) / b if b > 0 else 9e9) / bound(k))
    over = sorted(((min(v), st.median(v), max(v), len(v), k) for k, v in d.items()), reverse=True)
    s = '  '.join('%s%d: x%.2f..%.2f..%.2f (n=%d)' % (k[0], k[1], lo, me, hi, n) for lo, me, hi, n, k in over[:3] if hi >= 0.8)
    print('%-4s %s' % (p, s))
