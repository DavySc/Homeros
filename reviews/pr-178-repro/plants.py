#!/usr/bin/env python3
"""plants.py PLANTFILE [FROM]: each plant (tests/plants format) put into a copy of
src/org/org.c; test TESTS=org plants run tests/test_org.c, org-conform plants run
org_conform.py -o 2000 1 (the mode the open plants aim at; -o failing fails the
make target), sanitizers on.  One line per plant."""
import sys, os, subprocess, concurrent.futures as cf
W = '/tmp/claude-0/pl'; os.makedirs(W, exist_ok=True)
srcs = open('rv/srcs').read().split()
rest = [s for s in srcs if s != 'src/org/org.c']
flags = '-std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'.split()
def plant(kl):
    k, line = kl
    name, target, prep, file, old, new = line.rstrip('\n').split('§' if '§' in line else '~')
    old, new = old.replace('¶', '\n'), new.replace('¶', '\n')
    s = open(file).read()
    if s.count(old) != 1: return name, 'BROKEN (applies %d times)' % s.count(old)
    d = os.path.join(W, 'p%03d' % k); os.makedirs(d + '/src/org', exist_ok=True)
    for f in os.listdir('src/org'):
        open(d + '/src/org/' + f, 'wb').write(open('src/org/' + f, 'rb').read() if f != 'org.c' else s.replace(old, new).encode())
    main = 'tests/test_org.c' if target.startswith('test') else 'tests/org_drv.c'
    p = subprocess.run(['gcc'] + flags + ['-o', d + '/x', main, d + '/src/org/org.c'] + rest, capture_output=True)
    if p.returncode: return name, 'BUILD FAILED ' + p.stderr[-300:].decode()
    if target.startswith('test'):
        p = subprocess.run([d + '/x'], capture_output=True, timeout=900)
        out = (p.stdout + p.stderr).decode('utf-8', 'replace').strip().split('\n')
        hit = [l for l in out if 'CHECK(' in l or 'ERROR' in l or 'differ' in l]
        return name, ('caught: ' + (hit[0] if hit else out[-1])[:110]) if p.returncode else 'NOT CAUGHT'
    p = subprocess.run(['python3', 'tools/org_conform.py', '-o', d + '/x', '2000', '1'], capture_output=True, env=dict(os.environ, TMPDIR=d))
    last = (p.stdout.decode('utf-8', 'replace').strip().split('\n') or ['?'])[-1] or p.stderr[-200:].decode('utf-8', 'replace')
    return name, ('caught by -o: ' + last[:90]) if p.returncode else 'NOT CAUGHT by -o'
lines = [l for l in open(sys.argv[1]) if l.strip() and not l.startswith('#')]
if len(sys.argv) > 2: lines = [l for l in lines if sys.argv[2] in l]
with cf.ThreadPoolExecutor(3) as ex:
    for name, r in ex.map(plant, enumerate(lines)):
        print('%-88s %s' % (name[:88], r), flush=True)
