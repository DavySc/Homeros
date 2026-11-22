"""PR 98 review: gen2.py's texts with a habit's /N (well formed and not) after the repeaters.  habit.py SRC DST SEED"""
import glob, os, random, re, sys
src, dst, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
r = random.Random(seed)
os.makedirs(dst, exist_ok=True)
for p in sorted(glob.glob(src + '/*.org')):
    t = open(p, encoding='utf-8', newline='').read()
    t = re.sub(r'(\+[0-9][hdwmy])', lambda m: m.group(1) + r.choice(['/3d', '/2w', '/10d', '/3', '/', '/3d ', '/3dx', '/3d -1d', '/1h', '/3d>', '/3d\n', '/3d/4d', '']), t)
    t = re.sub(r'/3d>> ', '/3d>', t)
    q = os.path.join(dst, os.path.basename(p))
    open(q, 'w', encoding='utf-8', newline='').write(t)
    pos, st = open(p + '.cmd').read().split()
    open(q + '.cmd', 'w').write('%d %s\n' % (min(int(pos), len(t.encode())), st))
