# PR 78 probe: every catalogue snippet with fixed keys, nvim vs test_snip
import sys, os, subprocess, tempfile
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(__file__))
import snipmod as S
drv, md, keys = sys.argv[1], sys.argv[2], sys.argv[3].split(' ')
cases = [{'body': b, 'pre': '', 'keys': keys} for b in S.catalogue(md)]
with ThreadPoolExecutor(os.cpu_count()) as x: nv = [w for p in x.map(S.nvim1, [[c] for c in cases]) for w in p]
keep = [i for i in range(len(cases)) if nv[i] not in ('refused', 'hang')]
with tempfile.NamedTemporaryFile('w', delete=False) as f:
    for i in keep: f.write('%s\n%s\n%s\n' % (S.esc(cases[i]['body']), '', ' '.join(keys)))
ours = subprocess.run([drv, f.name], capture_output=True).stdout.decode().split('\n')
bad = 0
for j, i in enumerate(keep):
    w = nv[i].split(' ', 3); want = '%s %s %s %s' % (w[0], w[1], w[2], S.esc(w[3]))
    if want != ours[j]:
        bad += 1
        if bad <= 3:
            d = next(k for k in range(len(want)) if k >= len(ours[j]) or want[k] != ours[j][k]); print(repr(cases[i]["body"][:300])); print(" nvim", want[max(0,d-60):d+60]); print(" ours", ours[j][max(0,d-60):d+60])
print('%s: %d of %d differ (%d refused)' % (' '.join(keys), bad, len(keep), len(cases) - len(keep)))
