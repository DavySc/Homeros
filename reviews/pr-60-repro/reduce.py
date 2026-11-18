# shrinks an input on which drv and sqldiff fmt differ: drops token runs while they still differ
# usage: reduce.py DRV SQLDIFF FILE   prints the reduced input
import re, subprocess, sys, tempfile, os
drv, ref, f = sys.argv[1:4]
def differ(s):
    p = tempfile.mktemp(suffix='.sql'); open(p, 'wb').write(s)
    x = subprocess.run([drv, p], capture_output=True)
    y = subprocess.run([ref, 'fmt', '-d', 'tsql', '--keyword-case', 'lower', p], capture_output=True)
    os.remove(p)
    return x.returncode != 0 or x.stdout != y.stdout.replace(b'\r', b'')
t = re.findall(rb'\s+|\w+|.', open(f, 'rb').read(), re.S)
n = max(len(t) // 2, 1)
while n >= 1:
    i = 0; changed = False
    while i < len(t):
        u = t[:i] + t[i + n:]
        if differ(b''.join(u)): t = u; changed = True
        else: i += n
    if n == 1 and not changed: break
    n = max(n // 2, 1) if n > 1 else 1
sys.stdout.buffer.write(b''.join(t))
