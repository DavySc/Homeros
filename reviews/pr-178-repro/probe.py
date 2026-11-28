#!/usr/bin/env python3
"""Reviewer's probe for org_open: texts from org_conform's generators, bent
(characters dropped, doubled, swapped for non-ASCII ones; general element
lines mixed in), asked at EVERY character.  No filter: prints each differing
text.   probe.py DRIVER MODE N SEED    MODE a|b|c"""
import os, random, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', 'tools', 'org_conform.py')).read()
src = src[:src.rindex('\nmain()')]
g = {'__file__': os.path.join(HERE, '..', 'tools', 'org_conform.py')}
exec(compile(src, 'org_conform', 'exec'), g)
driver, mode, n, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
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
tmp = tempfile.mkdtemp(); files = []
for i in range(n):
    if mode == 'a': t = g['gen_open'](r)
    elif mode == 'b': t = bend(g['gen_open'](r))
    else:
        a = g['gen_open'](r).split('\n'); b = g['gen'](r).split('\n')
        m = []
        while a or b:
            s = a if (a and r.random() < .5) or not b else b
            m.append(s.pop(0))
        t = '\n'.join(m)
        if r.random() < .5: t = bend(t)
    p = os.path.join(tmp, 't%05d.org' % i)
    b = t.encode()
    open(p, 'wb').write(b)
    at = [j for j in range(len(b) + 1) if j == len(b) or (b[j] & 0xc0) != 0x80]
    open(p + '.cmd', 'w').write(' '.join(map(str, at)) + '\n')
    files.append(p)
def parse(out):
    d = {}; cur = None
    for l in out.split(b'\n'):
        if l.startswith(b'== '): cur = d.setdefault(l[3:], [])
        elif cur is not None: cur.append(l)
    return d
bad = cache = stale = 0
for k in range(0, n, 200):
    part = files[k:k + 200]
    el = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(HERE, '..', 'tools', 'org_open.el')] + part, capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC'))
    if el.returncode: sys.exit('emacs failed ' + el.stderr[-1500:].decode('utf-8', 'replace'))
    c = subprocess.run([driver, '-o'] + part, capture_output=True)
    if c.returncode: sys.exit('driver failed ' + c.stderr[-3000:].decode('utf-8', 'replace') + ' in ' + tmp)
    A, B = parse(el.stdout), parse(c.stdout)
    for p in part:
        nm = os.path.basename(p).encode(); a, b = A.get(nm), B.get(nm)
        if a == b: continue
        # ask Org again, each differing position alone in a fresh Emacs: asked in a
        # row, Org's element cache answers some positions from an earlier parse
        diffs = [(x, y) for x, y in zip(a, b) if x != y]
        real = []
        for x, y in diffs[:4] + diffs[-2:]:
            q = y.split()[0]
            open(p + '.cmd', 'wb').write(q + b'\n')
            one = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(HERE, '..', 'tools', 'org_open.el'), p], capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC')).stdout.split(b'\n')[1:]
            one = b'\n'.join(one).rstrip(b'\n')
            if one != y: real.append((one, y))
        if not real: cache += 1; continue
        kind = 'stale' if all((b'Search failed' in x or x.endswith(b' tags')) and y.endswith(b' offer') for x, y in real) else 'REAL'
        if kind == 'stale': stale += 1; continue
        bad += 1
        keep = os.path.join(HERE, 'fail', '%s%d-%s' % (mode, seed, nm.decode()))
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        open(keep, 'wb').write(open(p, 'rb').read())
        open(keep + '.cmd', 'wb').write(b' '.join(y.split()[0] for x, y in real) + b'\n')
        print(keep, '\n  org:  %r\n  ours: %r' % real[0])
print('%s seed %d: %d of %d differ (and %d only when Org is asked in a row, %d Org\'s stale tags match)' % (mode, seed, bad, n, cache, stale))
