"""PR 98 review: the smallest lines of FILE (and steps of FILE.cmd) on which
the driver still differs from Org.  reduce.py DRIVER FILE OUT"""
import os, subprocess, sys, tempfile
drv, f, out = sys.argv[1:4]
pos, steps = open(f + '.cmd').read().split()
raw = open(f, 'rb').read()
lines = raw.split(b'\n')
# the headline the position is in
off = 0; hl = 0
for i, l in enumerate(lines):
    if l.startswith(b'*') and off <= int(pos): hl = i
    off += len(l) + 1
tmp = tempfile.mkdtemp()
def split(b):
    return b.split(b'\n')[1:]
def differs(ls, h, st):
    p = os.path.join(tmp, 'x.org')
    open(p, 'wb').write(b'\n'.join(ls))
    open(p + '.cmd', 'w').write('%d %s\n' % (len(b'\n'.join(ls[:h])) + (1 if h else 0), st))
    a = split(subprocess.run(['emacs', '-Q', '--batch', '-l', 'tools/org_todo.el', p], capture_output=True, stdin=subprocess.DEVNULL, env=dict(os.environ, TZ='UTC')).stdout)
    b = split(subprocess.run([drv, '-t', p], capture_output=True).stdout)
    i = next((i for i, l in enumerate(a) if l.startswith(b'!error')), None)
    if i is not None: a, b = a[:i], b[:i]
    return a != b
assert differs(lines, hl, steps)
ch = True
while ch:
    ch = False
    for i in range(len(lines) - 1, -1, -1):
        if i == hl: continue
        c = lines[:i] + lines[i + 1:]
        h = hl - (i < hl)
        if differs(c, h, steps): lines, hl, ch = c, h, True
    for i in range(len(steps)):
        s = steps[:i] + steps[i + 1:]
        if s and differs(lines, hl, s): steps, ch = s, True; break
open(out, 'wb').write(b'\n'.join(lines))
open(out + '.cmd', 'w').write('%d %s\n' % (len(b'\n'.join(lines[:hl])) + (1 if hl else 0), steps))
