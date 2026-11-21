"""PR 93 review: the texts `org_conform.py -t DRIVER N SEED` makes, written to DIR (same random draws).  gen1.py DIR N SEED"""
import os, random, sys
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/org_conform.py')).read()
G = {'__file__': os.path.abspath(__file__)}
exec(src[:src.rindex('\nmain()')], G)
d, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
r = random.Random(seed)
os.makedirs(d, exist_ok=True)
for i in range(n):
    p = os.path.join(d, 't%05d.org' % i)
    t = G['gen_todo'](r)
    open(p, 'w', encoding='utf-8', newline='').write(t)
    open(p + '.cmd', 'w').write('%d %s\n' % (len(t[:r.randint(0, len(t))].encode()), ''.join(r.choice('rrl') for _ in range(r.randint(1, 4)))))
