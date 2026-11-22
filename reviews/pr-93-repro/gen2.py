"""PR 93 review: tools/org_conform.py's TODO texts with wider dates (any
month, days 1 to 31, years around the clock's, month ends, leap days),
tabs in the indentation, and more steps.  gen2.py DIR N SEED MODE
MODE: plain (the PR's own generator), wide (the dates, days up to 31 in any month), valid (the dates, only ones that exist), tab (indentation)"""
import os, random, re, sys
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/org_conform.py')).read()
src = src[:src.rindex('\nmain()')] if '\nmain()' in src else src
G = {'__file__': os.path.abspath(__file__)}
exec(src.replace("if __name__ == '__main__':", "if False:"), G)
d, n, seed, mode = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
r = random.Random(seed)
WD = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
def wide(m):
    y = r.choice([2026, 2026, 2026, 2025, 2027, 2024, 2028, 2020, 2000, 1999, 1970, 1969, 2038, 2100, 1900])
    mo = r.choice([r.randint(1, 12), 1, 2, 12, 11, 10])
    da = r.choice([r.randint(1, 31), 28, 29, 30, 31, 1])
    if mode == 'valid':
        import calendar
        da = min(da, calendar.monthrange(y, mo)[1])
    return '%04d-%02d-%02d' % (y, mo, da)
os.makedirs(d, exist_ok=True)
for i in range(n):
    t = G['gen_todo'](r)
    if mode in ('wide', 'valid'):
        t = re.sub(r'2026-11-\d\d', wide, t)
    if mode == 'tab':
        t = '\n'.join((r.choice(['\t', ' \t', '\t  ', '        ', '         ', '   ']) + l if l and l[0] != '*' and r.random() < .5 else l) for l in t.split('\n'))
    p = os.path.join(d, 't%05d.org' % i)
    with open(p, 'w', encoding='utf-8', newline='') as g:
        g.write(t)
    with open(p + '.cmd', 'w') as g:
        g.write('%d %s\n' % (len(t[:r.randint(0, len(t))].encode()), ''.join(r.choice('rrl') for _ in range(r.randint(1, 6)))))
