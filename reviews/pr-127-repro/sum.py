# sum.py <prefix> <name>...: per run of each binary, what failed and by how much
# a timed gate fails when its best try is at or over its bound; margin = best try / bound
import sys, glob, re
LB = 100000
GB = [20000, 40000, 20000, 20000, 20000, 20000, 20000, 20000]
pre = sys.argv[1]
for n in sys.argv[2:]:
    rows = []
    for f in sorted(glob.glob('out/%s-%s-*.txt' % (pre, n)), key=lambda s: int(re.findall(r'-(\d+)\.txt', s)[0])):
        best, R, chk = {}, None, ''
        for ln in open(f):
            w = ln.split()
            if not w: continue
            if w[0] in 'LG' and len(w) == 4:
                k = (w[0], int(w[1])); best[k] = min(best.get(k, 1 << 60), int(w[3]))
            elif w[0] == 'R': R = int(w[2]) / int(w[1])
            elif 'CHECK' in ln: chk = re.findall(r':(\d+): CHECK\((.*)\) failed', ln)[0]
        over = {k: v / (LB if k[0] == 'L' else GB[k[1]]) for k, v in best.items()}
        worst = max(over.items(), key=lambda kv: kv[1])
        rows.append('%s%d %.2f%s%s' % (worst[0][0], worst[0][1], worst[1], ' R %.2f' % R if R is not None else '', ' FAIL ' + chk[1][:22] if chk else ''))
    print(n.ljust(5), ' | '.join(rows))
