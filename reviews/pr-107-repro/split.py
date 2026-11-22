# split a trace into scenarios, replay each alone, classify the first difference
import subprocess, sys, re, collections
tf, trace = sys.argv[1], sys.argv[2]
scen = []; cur = []
for ln in open(trace):
    if ln.startswith('S ') and cur: scen.append(cur); cur = []
    cur.append(ln)
scen.append(cur)
kinds = collections.Counter(); ex = {}
for i, s in enumerate(scen):
    open('one.txt', 'w').writelines(s)
    p = subprocess.run([tf, 'one.txt'], capture_output=True, text=True)
    if p.returncode == 0: kinds['same'] += 1; continue
    m = re.search(r'line (\d+): (\S+), nvim (\S+)', p.stderr)
    if not m: kinds['other: ' + p.stderr[:60]] += 1; continue
    got, want = m.group(2), m.group(3)
    k = 'level differs' if got.split('/')[0] != want.split('/')[0] else ('nvim closed, module open' if '/' in want and '/' not in got else 'nvim open, module closed' if '/' in got and '/' not in want else 'closed range differs')
    kinds[k] += 1; ex.setdefault(k, (i, p.stderr.strip()))
for k, v in kinds.most_common(): print(v, k, ex.get(k, ''))
