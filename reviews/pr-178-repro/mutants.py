#!/usr/bin/env python3
"""mutants.py PLANTFILE N SEED [NAME]: for each plant, how many of 3N bent texts
(probe.py's modes a, b, c, N each, asked at every character) its driver answers
differently from the tree's driver; the first such text is kept in rv/mut/.
No Emacs: a mutant that differs here is not equivalent, and Org is then asked
by hand which of the two is right."""
import os, random, subprocess, sys, concurrent.futures as cf
HERE = os.path.dirname(os.path.abspath(__file__))
src = open('tools/org_conform.py').read(); src = src[:src.rindex('\nmain()')]
g = {'__file__': 'tools/org_conform.py'}; exec(compile(src, 'org_conform', 'exec'), g)
pf, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
r = random.Random(seed)
UNI = ['é', ' ', '٣', 'ß', '中', ' ', 'É', '²', '́', '\t', '\r', '\f', 'É:', 'x', '"', "'", '(', ')', '{', '}', '-', ']', '[', '<', '>', ':', '*', '\\', '#', '|', '\n', '\n\n', ' ']
def bend(t):
    t = list(t)
    for _ in range(r.randint(1, 6)):
        if not t: break
        i = r.randrange(len(t)); k = r.random()
        if k < .3: del t[i]
        elif k < .5: t.insert(i, t[i])
        elif k < .9: t.insert(i, r.choice(UNI))
        else: t[i] = r.choice(UNI)
    return ''.join(t)
C = '/tmp/claude-0/corpus-%d-%d' % (n, seed); files = []
fresh = not os.path.isdir(C); os.makedirs(C, exist_ok=True)
for mode in 'abc':
    for i in range(n):
        if mode == 'a': t = g['gen_open'](r)
        elif mode == 'b': t = bend(g['gen_open'](r))
        else:
            a = g['gen_open'](r).split('\n'); b = g['gen'](r).split('\n'); m = []
            while a or b:
                s = a if (a and r.random() < .5) or not b else b
                m.append(s.pop(0))
            t = '\n'.join(m)
            if r.random() < .5: t = bend(t)
        p = os.path.join(C, '%s%05d.org' % (mode, i)); files.append(p)
        if fresh:
            b = t.encode(); open(p, 'wb').write(b)
            open(p + '.cmd', 'w').write(' '.join(str(j) for j in range(len(b) + 1) if j == len(b) or (b[j] & 0xc0) != 0x80) + '\n')
srcs = open('rv/srcs').read().split(); rest = [s for s in srcs if s != 'src/org/org.c']
flags = '-std=c17 -D_DEFAULT_SOURCE -Iinclude -O2'.split()
def out(drv):
    d = {}
    for k in range(0, len(files), 500):
        o = subprocess.run([drv, '-o'] + files[k:k + 500], capture_output=True)
        cur = None
        for l in o.stdout.split(b'\n'):
            if l.startswith(b'== '): cur = d.setdefault(l[3:], [])
            elif cur is not None: cur.append(l)
        if o.returncode: d[b'CRASH'] = [o.stderr[-200:]]
    return d
ref = out('rv/drv_o2')
W = '/tmp/claude-0/mu'; os.makedirs(W, exist_ok=True); os.makedirs('rv/mut', exist_ok=True)
def one(kl):
    k, line = kl
    name, target, prep, file, old, new = line.rstrip('\n').split('§' if '§' in line else '~')
    old, new = old.replace('¶', '\n'), new.replace('¶', '\n')
    s = open(file).read()
    if s.count(old) != 1: return name, 'BROKEN'
    d = os.path.join(W, 'm%03d' % k); os.makedirs(d + '/src/org', exist_ok=True)
    for f in os.listdir('src/org'):
        open(d + '/src/org/' + f, 'wb').write(open('src/org/' + f, 'rb').read() if f != 'org.c' else s.replace(old, new).encode())
    p = subprocess.run(['gcc'] + flags + ['-o', d + '/x', 'tests/org_drv.c', d + '/src/org/org.c'] + rest, capture_output=True)
    if p.returncode: return name, 'BUILD FAILED'
    m = out(d + '/x')
    if b'CRASH' in m: return name, 'CRASHES'
    dif = [f for f in ref if ref[f] != m.get(f)]
    if not dif: return name, 'same answers on all %d texts' % len(files)
    f = sorted(dif)[0]; j = next(j for j in range(len(ref[f])) if j >= len(m[f]) or ref[f][j] != m[f][j])
    slug = ''.join(c if c.isalnum() else '-' for c in name)[:50]
    for x in ('', '.cmd'):
        open('rv/mut/%s.org%s' % (slug, x), 'wb').write(open(os.path.join(C, f.decode()) + x, 'rb').read() if not x else ref[f][j].split()[0] + b'\n')
    return name, '%d of %d texts differ; first %s: tree %r, mutant %r' % (len(dif), len(files), f.decode(), ref[f][j][:60], m[f][j][:60] if j < len(m[f]) else b'')
lines = [l for l in open(pf) if l.strip() and not l.startswith('#')]
if len(sys.argv) > 4: lines = [l for l in lines if sys.argv[4] in l]
with cf.ThreadPoolExecutor(3) as ex:
    for name, res in ex.map(one, enumerate(lines)):
        print('%-70s %s' % (name[:70], res), flush=True)
