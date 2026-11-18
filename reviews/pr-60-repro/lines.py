# differential: random runs of whole lines of real T-SQL files, optionally with tokens dropped or doubled
# usage: windows.py DRV SQLDIFF SEED N FILES...   prints each differing input's path
import random, subprocess, sys, os, re
drv, ref, seed, n, files = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5:]
R = random.Random(seed)
data = [open(f, 'rb').read().replace(b'\r', b'') for f in files]
out = 'build/rv/win'; os.makedirs(out, exist_ok=True)
diff = 0
for i in range(n):
    d = R.choice(data); L = d.split(b'\n'); a = R.randrange(len(L)); s = b'\n'.join(L[a:a + R.randrange(1, 60)]) + b'\n'
    if R.random() < .5:
        t = re.findall(rb'\s+|\w+|.', s, re.S)
        for _ in range(R.randrange(1, 6)):
            if not t: break
            j = R.randrange(len(t)); r = R.random()
            if r < .4: del t[j]
            elif r < .7: t.insert(j, t[R.randrange(len(t))])
            else: t[j] = R.choice([b'BEGIN', b'END', b'ELSE', b'IF', b'(', b')', b'GO', b';', b'\n', b':', b'TRY', b'CATCH', b'AS', b'SELECT', b'WITH', b'CASE'])
        s = b''.join(t)
    p = '%s/%d-%d.sql' % (out, seed, i); open(p, 'wb').write(s)
    x = subprocess.run([drv, p], capture_output=True)
    y = subprocess.run([ref, 'fmt', '-d', 'tsql', '--keyword-case', 'lower', p], capture_output=True)
    if x.returncode or x.stdout != y.stdout.replace(b'\r', b''): diff += 1; print('DIFF', p, x.returncode, flush=True)
    else: os.remove(p)
print('seed %d: %d inputs, %d differ' % (seed, n, diff))
