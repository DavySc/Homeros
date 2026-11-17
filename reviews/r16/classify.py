"""Groups cmp.py's first differences by kind: classify.py OUT/x.diff..."""
import sys, collections
for f in sys.argv[1:]:
    c = collections.Counter(); ex = {}
    L = open(f, 'rb').read().split(b'\n')
    for i in range(0, len(L) - 2, 3):
        org = L[i + 1][8:].split(b' '); ours = L[i + 2][8:].split(b' ')
        key = (org[0], ours[0]) if org[0] != ours[0] else (org[0], tuple(j for j in range(min(len(org), len(ours))) if org[j] != ours[j]))
        c[key] += 1; ex.setdefault(key, L[i].decode() + ' | ' + L[i + 1].decode() + ' | ' + L[i + 2].decode())
    print(f)
    for k, v in c.most_common(): print(' ', v, k, ex[k])
