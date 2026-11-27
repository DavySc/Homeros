import sys, statistics as st
for f in sys.argv[1:]:
    d = {}
    for ln in open(f):
        w = ln.split()
        if len(w) == 5 and w[0] in 'AB': d.setdefault(w[0] + w[1], []).append(float(w[3]) / float(w[4]))
    print(f.split('/')[-1].ljust(22), '  '.join('%s %.2f..%.2f..%.2f' % (k, min(v), st.median(v), max(v)) for k, v in sorted(d.items())), 'n=%d' % len(next(iter(d.values()))))
