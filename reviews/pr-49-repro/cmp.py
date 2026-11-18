import subprocess, sys, os, glob
def split(b):
    d, cur = {}, None
    for l in b.decode('utf-8', 'replace').split('\n'):
        if l.startswith('== '): cur = l[3:]; d[cur] = []
        elif l: d[cur].append(l)
    return d
drv, dir_ = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(dir_ + '/*.org'))
bad = tot = errs = 0
for k in range(0, len(files), 500):
    part = files[k:k+500]
    a = split(subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ts_dump.el')] + part, capture_output=True).stdout)
    c = split(subprocess.run([drv] + part, capture_output=True, check=True).stdout)
    for p in part:
        nm = os.path.basename(p)
        x, y = a.get(nm, []), c.get(nm, [])
        tot += sum(1 for l in x if l.startswith('@'))
        if any(l.startswith('!error') for l in x): errs += 1; continue
        if x != y:
            bad += 1
            if bad <= 8:
                print(nm, repr(open(p, 'rb').read()))
                for l in sorted(set(x) ^ set(y)): print('  ', 'org ' if l in x else 'ours', l)
print('%d of %d files differ, %d Org timestamps, Org errored on %d' % (bad, len(files), tot, errs))
