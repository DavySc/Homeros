"""PR 98 review: Org's answer and the driver's for FILES, first differing line.  show.py DRIVER FILES"""
import os, subprocess, sys
drv, files = sys.argv[1], sys.argv[2:]
def split(b):
    out, cur = {}, None
    for l in b.split(b'\n'):
        if l.startswith(b'== '): cur = l[3:]; out[cur] = []
        elif cur is not None: out[cur].append(l)
    return out
el = split(subprocess.run(['emacs', '-Q', '--batch', '-l', 'tools/org_todo.el'] + files, capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC')).stdout)
c = split(subprocess.run([drv, '-t'] + files, capture_output=True).stdout)
for p in files:
    a, b = el[os.path.basename(p).encode()], c[os.path.basename(p).encode()]
    i = next((i for i, l in enumerate(a) if l.startswith(b'!error')), None)
    if i is not None: a, b = a[:i], b[:i]
    if a == b: print(p, 'same'); continue
    j = next(j for j in range(max(len(a), len(b))) if j >= len(a) or j >= len(b) or a[j] != b[j])
    print(p, open(p + '.cmd').read().strip(), 'line', j + 1, '\n  org :', a[j] if j < len(a) else None, '\n  ours:', b[j] if j < len(b) else None)
