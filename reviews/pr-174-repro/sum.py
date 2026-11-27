#!/usr/bin/env python3
"""sum.py FILE...: per row, the ratio's min, median, max over runs; exits"""
import sys, statistics as st
for f in sys.argv[1:]:
    rows, rc = {}, []
    for l in open(f):
        p = l.split()
        if p[0] == 'RC': rc.append(int(p[2]))
        elif p[1] == 'ROW': rows.setdefault(int(p[2]), []).append((float(p[5]), float(p[3]), float(p[4])))
    print('%s: %d runs, %d failed' % (f.split('/')[-1], len(rc), sum(1 for r in rc if r)))
    for i in sorted(rows):
        r = sorted(x[0] for x in rows[i])
        print('  row %d ratio min %.2f med %.2f max %.2f   short ms med %.2f long ms med %.2f   >=8: %d  <8: %d' % (i, r[0], st.median(r), r[-1], st.median(x[1] for x in rows[i]), st.median(x[2] for x in rows[i]), sum(1 for x in r if x >= 8), sum(1 for x in r if x < 8)))
