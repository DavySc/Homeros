# the org-conform plants of tests/plants/org.txt against the oracle in the tree:
# each planted into a copy of src/org/org.c, the driver linked from objects built
# once, then make org-conform's four runs (ORG_N 2000, SEED 1), to the first failure
import sys, os, re, shlex, subprocess, concurrent.futures as cf
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
tree = os.getcwd(); W = os.path.join(S, 'pl'); os.makedirs(W, exist_ok=True)
cc = shlex.split(open(os.path.join(S, 'h', 'cc.txt')).read())
srcs = [a for a in cc if a.endswith('.c') and (a.startswith('src/') or a.startswith('ref/')) and a != 'src/org/org.c']
flags = cc[1:cc.index('-o')]
objs = []
def obj(s):
    o = os.path.join(W, s.replace('/', '_')[:-2] + '.o')
    if not os.path.exists(o): subprocess.run(['gcc'] + flags + ['-c', s, '-o', o], check=True)
    return o
with cf.ThreadPoolExecutor(4) as ex: objs = list(ex.map(obj, srcs))
rest = [a for a in cc[cc.index('-o') + 2:] if a not in srcs and a not in ('tests/org_drv.c', 'src/org/org.c')]
def plant(k_line):
    k, line = k_line
    name, target, prep, file, old, new = line.rstrip('\n').split('~')
    old, new = old.replace('¶', '\n'), new.replace('¶', '\n')
    d = os.path.join(W, 'p%03d' % k); os.makedirs(d + '/src/org', exist_ok=True)
    s = open(file).read()
    if file != 'src/org/org.c' or s.count(old) != 1 or prep: return k, name, 'SKIPPED (not a plain org.c plant)'
    for f in os.listdir('src/org'):
        t = open('src/org/' + f, 'rb').read()
        open(d + '/src/org/' + f, 'wb').write(t if f != 'org.c' else s.replace(old, new).encode())
    drv = d + '/org_drv'
    p = subprocess.run(['gcc'] + flags + ['-o', drv, 'tests/org_drv.c', d + '/src/org/org.c'] + objs + rest, capture_output=True)
    if p.returncode: return k, name, 'BUILD FAILED ' + p.stderr[-300:].decode()
    n = int(re.search(r'ORG_N=(\d+)', target).group(1)); seed = re.search(r'SEED=(\d+)', target).group(1)
    for mode, m in (('', n), ('-f', n), ('-t', n // 4), ('-s', n)):
        p = subprocess.run(['python3', 'tools/org_conform.py'] + ([mode] if mode else []) + [drv, str(m), seed], capture_output=True, cwd=tree, env=dict(os.environ, TMPDIR=d))
        last = p.stdout.decode('utf-8', 'replace').strip().split('\n')[-1] if p.stdout.strip() else p.stderr[-200:].decode('utf-8', 'replace')
        if p.returncode:
            ok = re.search(r'\b[1-9][0-9]* of [0-9]+ differ', last) or 'driver failed' in last
            return k, name, ('caught by %s: %s' % (mode or 'parse', last[:90])) if ok else 'FAILED OTHERWISE: ' + last[:200]
    return k, name, 'NOT CAUGHT'
lines = [l for l in open('tests/plants/org.txt') if 'org-conform' in l.split('~')[1:2][0:1].__str__() and not l.startswith('#')]
with cf.ThreadPoolExecutor(4) as ex:
    for k, name, r in ex.map(plant, enumerate(lines)):
        print('%3d %-70s %s' % (k, name[:70], r), flush=True)
