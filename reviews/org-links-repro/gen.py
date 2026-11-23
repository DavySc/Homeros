# big texts: random org texts from org_conform's generators, joined, so the walk
# passes its 4096-line checkpoints and the 64-line link look-back
import random, sys
sys.path.insert(0, sys.argv[1])
import types
oc = types.ModuleType('oc'); src = open(sys.argv[1] + '/org_conform.py').read().rsplit('main()', 1)[0]; oc.__dict__['__file__'] = sys.argv[1] + '/org_conform.py'; exec(src, oc.__dict__)
seed, out, lines = int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
r = random.Random(seed)
parts, n = [], 0
while n < lines:
    t = (oc.gen_faces if r.random() < 0.5 else oc.gen_show)(r)
    if isinstance(t, str): t = t.encode()
    if r.random() < 0.3:   # a long paragraph with links and footnotes
        t += b''.join(r.choice([b'a ', b'[[a][b\n', b'c]] ', b'https://x.y ', b'[fn:: (x\n', b'y)] ', b'\n', b'<id:a\nb> ']) for _ in range(r.randint(10, 400))) + b'\n'
    parts.append(t); n += t.count(b'\n')
open(out, 'wb').write(b''.join(parts))
