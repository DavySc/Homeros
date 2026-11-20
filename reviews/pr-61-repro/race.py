#!/usr/bin/env python3
"""race.py HOMEROS N: two editors on a.txt and b.txt quit at the same moment, N rounds, each from an empty state dir.
Prints how often both, one or no file is kept, and any round whose oldfiles is malformed or leaves other files."""
import os, pty, re, select, shutil, sys, time
exe, n = os.path.abspath(sys.argv[1]), int(sys.argv[2])
d = '/var/tmp/pr61-race'; shutil.rmtree(d, ignore_errors=True); os.makedirs(d + '/w')
for f in 'ab': open('%s/w/%s.txt' % (d, f), 'w').write('x\n')
os.environ['XDG_STATE_HOME'] = d + '/state'
def start(f):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(d + '/w'); os.execv(exe, [exe, '-t', f])
    return pid, fd
def drain(fds, t):
    end = time.time() + t
    while time.time() < end:
        for fd in select.select(fds, [], [], 0.02)[0]:
            try: os.read(fd, 1 << 16)
            except OSError: pass
count = {0: 0, 1: 0, 2: 0}; bad = 0
for i in range(n):
    shutil.rmtree(d + '/state', ignore_errors=True)
    # the file must exist first, or both create it (not the race in question)
    p, fd = start('a.txt'); drain([fd], 0.3); os.write(fd, b':q\r'); os.waitpid(p, 0); os.close(fd)
    os.remove(d + '/state/homeros/oldfiles'); open(d + '/state/homeros/oldfiles', 'w').write('1 /seed\n')
    ps = [start('a.txt'), start('b.txt')]
    drain([fd for _, fd in ps], 0.4)
    for _, fd in ps: os.write(fd, b":q\r"); time.sleep(float(sys.argv[3]))
    for p, fd in ps: os.waitpid(p, 0); os.close(fd)
    s = open(d + '/state/homeros/oldfiles').read()
    ok = all(re.fullmatch(r'\d+ /.+', l) for l in s.splitlines()) and s.endswith('\n') and '/seed' in s
    extra = [f for f in os.listdir(d + '/state/homeros') if f not in ('oldfiles', 'journal')]
    if not ok or extra: bad += 1; print('round', i, repr(s), extra)
    count[('a.txt' in s) + ('b.txt' in s)] += 1
print('rounds %d: both kept %d, one kept %d, none %d; malformed or leftovers %d' % (n, count[2], count[1], count[0], bad))
