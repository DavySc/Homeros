# min3.py <check> <file>...: the check's ratio per run (one try), against the least org time over the least plain time of
# each 3 runs in a row (noise only adds time, so the least of 3 is the steadier estimate)
import sys
key = sys.argv[1]
for f in sys.argv[2:]:
    a = []
    for ln in open(f):
        w = ln.split()
        if len(w) == 5 and w[0] + w[1] == key: a.append((float(w[3]), float(w[4])))
    one = [x / y for x, y in a]
    g = [a[i:i + 3] for i in range(0, len(a) - 2, 3)]
    m3 = [min(x for x, _ in t) / min(y for _, y in t) for t in g]
    any3 = [min(x / y for x, y in t) for t in g]
    print('%-22s %s one try %.2f..%.2f (n=%d) | any of 3 (the PR) %.2f..%.2f | least/least of 3 %.2f..%.2f (n=%d)' % (f.split('/')[-1], key, min(one), max(one), len(one), min(any3), max(any3), min(m3), max(m3), len(m3)))
