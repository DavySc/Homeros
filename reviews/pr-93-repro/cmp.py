"""PR 93 review: org_todo_step against tools/org_todo.el on DIR/*.org,
leaving out the texts where Org stops (an error, or its "Continue?"
question, which batch Emacs cannot answer).  cmp.py DRIVER DIR [show]"""
import glob, os, pickle, subprocess, sys
drv, d = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(os.path.join(d, '*.org')))
def split(b):
    out, cur = {}, None
    for l in b.split(b'\n'):
        if l.startswith(b'== '):
            cur = l[3:]; out[cur] = []
        else:
            out[cur].append(l)
    return out
bad = skip = 0
for k in range(0, len(files), 400):
    part = files[k:k + 400]
    ck = os.path.join(d, 'el%s%d.pickle' % (os.environ.get('TAG', ''), k))   # Org's answers, kept: the mutants run against the same texts
    if os.path.exists(ck):
        el = pickle.load(open(ck, 'rb'))
    else:
        el = split(subprocess.run(['emacs', '-Q', '--batch', '-l', 'tools/org_todo.el'] + part, capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC')).stdout)
        pickle.dump(el, open(ck, 'wb'))
    c = subprocess.run([drv, '-t'] + part, capture_output=True)
    if c.returncode: print('driver failed (counts as a difference)'); sys.exit('driver failed on part %d: %s' % (k, c.stderr[-3000:].decode('utf-8', 'replace')))
    c = split(c.stdout)
    for p in part:
        n = os.path.basename(p).encode()
        a, b = el[n], c[n]
        if any(b'!error' in l or b'Continue?' in l for l in a): skip += 1; continue
        if a != b:
            bad += 1
            if len(sys.argv) > 3 and bad <= int(sys.argv[3]):
                j = next(j for j in range(max(len(a), len(b))) if j >= len(a) or j >= len(b) or a[j] != b[j])
                print(p, open(p + '.cmd').read().strip(), 'line', j + 1, '\n  org :', a[j] if j < len(a) else None, '\n  ours:', b[j] if j < len(b) else None)
print('%d of %d differ, %d left out (Org stops)' % (bad, len(files), skip))
