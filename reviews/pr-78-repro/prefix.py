# PR 78 probe: replay each prefix of one case's keys in nvim and in test_snip, print both
import sys, json, subprocess, tempfile, os
sys.path.insert(0, os.path.dirname(__file__))
import snipmod as S
drv, body, pre, keys = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split(' ') if sys.argv[4] else []
keys = [' ' if k == '<Space>' else k for k in keys]
cases = [{'body': body, 'pre': pre, 'keys': keys[:i]} for i in range(len(keys) + 1)]
nv = [S.nvim1([c])[0] for c in cases]
with tempfile.NamedTemporaryFile('w', delete=False) as f:
    for c in cases: f.write('%s\n%s\n%s\n' % (S.esc(c['body']), S.esc(c['pre']), ' '.join(t if t != ' ' else '<Space>' for t in c['keys'])))
ours = subprocess.run([drv, f.name], capture_output=True).stdout.decode().split('\n')
for i, c in enumerate(cases):
    w = nv[i].split(' ', 3); want = '%s %s %s %s' % (w[0], w[1], w[2], S.esc(w[3]))
    print('%-28s nvim %-30r ours %r %s' % (' '.join(c['keys']), want, ours[i], '' if want == ours[i] else '<<'))
