"""PR 98 review: the texts org_conform.py -t counts but does not compare
(Org wrote a time it cannot read back): what Org wrote, the error, and
whether ours agrees on the steps before the bad one.  skips.py DRIVER FILES"""
import os, re, subprocess, sys, collections
drv, files = sys.argv[1], sys.argv[2:]
def split(b):
    out, cur = {}, None
    for l in b.split(b'\n'):
        if l.startswith(b'== '): cur = l[3:]; out[cur] = []
        elif cur is not None: out[cur].append(l)
    return out
errs = collections.Counter(); shown = 0; n = 0; before_bad = 0
for k in range(0, len(files), 400):
    part = files[k:k + 400]
    el = split(subprocess.run(['emacs', '-Q', '--batch', '-l', 'tools/org_todo.el'] + part, capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC')).stdout)
    c = split(subprocess.run([drv, '-t'] + part, capture_output=True).stdout)
    for p in part:
        a, b = el[os.path.basename(p).encode()], c[os.path.basename(p).encode()]
        i = next((i for i, l in enumerate(a) if l.startswith(b'!error')), None)
        if i is None: continue
        errs[re.sub(rb'[0-9]+', b'N', a[i])[:70]] += 1
        m = [l for l in a[:i] if re.search(rb'[0-9]:(-|[0-9]{3})', l)]
        if not m: continue
        n += 1
        # steps before the last one (the text Org left broken) must still agree
        ends = [j for j, l in enumerate(a[:i]) if l == b'--']
        cut = ends[-2] + 1 if len(ends) > 1 else 0
        if a[:cut] != b[:cut]: before_bad += 1; print('EARLIER STEP DIFFERS', p)
        if shown < 4: shown += 1; print(p, open(p + '.cmd').read().strip(), m[0], a[i][:90])
print('skipped %d, earlier steps differ on %d' % (n, before_bad))
for e, v in errs.most_common(): print(v, e)
