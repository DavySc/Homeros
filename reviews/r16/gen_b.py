"""Build B's generator (tools/org_conform.py's gen), run alone: gen_b.py ORG_CONFORM_PY SEED COUNT DIR"""
import random, sys, os
src = open(sys.argv[1], encoding='utf-8').read()
ns = {'__file__': sys.argv[1]}
src = src[:src.index('def run(')]
if os.environ.get('NOCALL'): src = src.replace("'CALL', ", '')
exec(src, ns)
seed, n, out = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
os.makedirs(out, exist_ok=True)
r = random.Random(seed)
for i in range(n):
    with open(os.path.join(out, 'b%d-%05d.org' % (seed, i)), 'w', encoding='utf-8', newline='') as f:
        f.write(ns['gen'](r))
