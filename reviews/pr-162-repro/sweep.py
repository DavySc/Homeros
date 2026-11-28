# for each seed and mode: the texts org_conform makes, Org's answer with the old and
# the new advice file; the texts whose answer changed, and what the error is
import sys, os, re, random, subprocess, collections
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(S, 'new', 'org_conform.py'), encoding='utf-8').read()
src = src[:src.rindex('main()')]
g = {'__file__': os.path.join(S, 'new', 'org_conform.py'), '__name__': 'x'}
exec(compile(src, 'org_conform', 'exec'), g)
mode, n, seeds = sys.argv[1], int(sys.argv[2]), [int(x) for x in sys.argv[3:]]
gen = g['gen_faces'] if mode == 'f' else g['gen_show']
el = 'org_faces.el' if mode == 'f' else 'org_show.el'
def emacs(which, files):
    out = {}
    for k in range(0, len(files), 400):
        p = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(S, which, el)] + files[k:k+400],
                           capture_output=True, env=dict(os.environ, TZ='UTC'))
        assert p.returncode == 0, p.stderr[-500:]
        cur = None
        for l in p.stdout.split(b'\n'):
            if l.startswith(b'== '):
                cur = l[3:].decode(); out[cur] = []
            elif cur is not None and l:
                out[cur].append(l)
        out.setdefault('#stderr', []).append(p.stderr)
    return out
tot = collections.Counter(); errs = collections.Counter(); keep = os.path.join(S, 'newly-' + mode)
os.makedirs(keep, exist_ok=True)
for seed in seeds:
    r = random.Random(seed)
    d = os.path.join(S, 'texts-%s-%d' % (mode, seed)); os.makedirs(d, exist_ok=True)
    files = []
    for i in range(n):
        p = os.path.join(d, 't%05d.org' % i)
        open(p, 'w', encoding='utf-8', newline='').write(gen(r)); files.append(p)
    a, b = emacs('old', files), emacs('new', files)
    msgs = b''.join(a['#stderr']).count(b'Org mode fontification error')
    tot['fontification-messages-old'] += msgs
    tot['fontification-messages-new'] += b''.join(b['#stderr']).count(b'Org mode fontification error')
    for p in files:
        name = os.path.basename(p)
        ea = any(l.startswith(b'!error') for l in a[name]); eb = any(l.startswith(b'!error') for l in b[name])
        tot['texts'] += 1; tot['old-error'] += ea; tot['new-error'] += eb
        if a[name] != b[name]:
            tot['answer-changed'] += 1
            if eb and not ea:
                tot['newly-error'] += 1
                e = next(l for l in b[name] if l.startswith(b'!error')).decode('utf-8', 'replace')
                errs[re.sub(r'\d+', 'N', e)[:110]] += 1
                os.link(p, os.path.join(keep, '%d-%s' % (seed, name))) if not os.path.exists(os.path.join(keep, '%d-%s' % (seed, name))) else None
            elif ea and eb:
                tot['both-error-other-message'] += 1
            else:
                tot['changed-without-new-error'] += 1; print('CHANGED', p)
print(mode, dict(tot)); [print('  %5d %s' % (c, e)) for e, c in errs.most_common()]
