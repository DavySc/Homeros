#!/usr/bin/env python3
"""Writes shada-expect.inc: Neovim's v:oldfiles after random sessions, for
tests/test_shada.c.

conform.py FAKE_TIME_SO SEED N > shada-expect.inc

Each scenario starts with no ShaDa file and runs three sessions of Neovim 0.12
(on PATH) in a pty, as Davy runs it: opened on a file or none, then :e and
:bd of files, two seconds apart (and two more before :qa) on a clock that tools/fake_time.c fakes
(HOMEROS_TIME), then :qa.  After each session a headless Neovim prints
v:oldfiles.  The files live in /var/tmp/homeros-shada/ (the test makes the
same ones); one is in /tmp/ ('shada' r/tmp/) and one never exists."""
import fcntl, os, pty, random, select, struct, subprocess, sys, termios, time

shim, seed, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
D = '/var/tmp/homeros-shada'
FILES = [D + '/a', D + '/b', D + '/c.txt', D + '/sub/d', D + '/e', D + '/new', '/tmp/homeros-shada-t']
SHADA = D + '/main.shada'
os.makedirs(D + '/sub', exist_ok=True)
for f in FILES:
    if not f.endswith('/new'):
        open(f, 'w').write('x\ny\n')

def session(start, acts, t0):
    """start: a FILES index or -1; acts: ('e', i), ('bd',); returns the clock after"""
    env = dict(os.environ, TERM='xterm-256color', LD_PRELOAD=shim, HOMEROS_TIME=str(t0))
    argv = ['nvim', '-u', 'NONE', '-n', '-i', SHADA] + ([FILES[start]] if start >= 0 else [])
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(D)
        os.execvpe('nvim', argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))

    def drain(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if r:
                try:
                    os.read(fd, 1 << 16)
                except OSError:
                    return
    drain(0.4)
    t = t0
    for a in acts:
        t += 2
        cmd = ':let $HOMEROS_TIME=%d\r' % t + (':e %s\r' % FILES[a[1]] if a[0] == 'e' else ':bd\r')
        os.write(fd, cmd.encode())
        drain(0.15)
    t += 2
    os.write(fd, (':let $HOMEROS_TIME=%d\r:qa\r' % t).encode())
    drain(0.3)
    os.waitpid(pid, 0)
    return t

def oldfiles():
    out = subprocess.run(['nvim', '-u', 'NONE', '--headless', '-i', SHADA, '-c',
                          'call writefile(v:oldfiles, "/dev/stdout")', '-c', 'qa!'],
                         capture_output=True, text=True).stdout
    return [l for l in out.split('\n') if l]

random.seed(seed)
print('/* Neovim\'s v:oldfiles after each of three sessions, by src/shada/conform.py %d %d:' % (seed, n))
print('   per session the file opened (-1 none), the commands (\'e\' and a file,')
print('   \'d\' :bd), the clock at start, and v:oldfiles as indexes into files[] */')
print('static const char *const files[] = { %s };' % ', '.join('"%s"' % f for f in FILES))
print('static const struct { int start; char acts[16]; long t0; int nold; int old[16]; } sessions[][3] = {')
for k in range(n):
    if os.path.exists(SHADA):
        os.remove(SHADA)
    t, row = 1000, []
    for s in range(3):
        start = random.randrange(-1, len(FILES))
        acts = [('e', random.randrange(len(FILES))) if random.randrange(4) else ('bd',) for _ in range(random.randrange(5))]
        t0 = t
        t = session(start, acts, t0) + 10
        old = [FILES.index(p) for p in oldfiles()]
        code = ''.join('e%d' % a[1] if a[0] == 'e' else 'd' for a in acts)
        row.append('{ %d, "%s", %d, %d, { %s } }' % (start, code, t0, len(old), ', '.join(map(str, old)) or '0'))
    print('\t{ ' + ', '.join(row) + ' },')
print('};')
