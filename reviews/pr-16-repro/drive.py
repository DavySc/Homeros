# Drives ./homeros -t in a pty.  The screen reader is the one in the author's
# package-11/e2e_crash.py; the scenarios are the review's.
#   python3 reviews/pr-16-repro/drive.py ./homeros <scenario>
import os, pty, sys, time, signal, select, tempfile, re, shutil, glob
ed = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp()
state = d + '/state'
os.environ['XDG_STATE_HOME'] = state
screen = [[' '] * 80 for _ in range(24)]

def run(f, env=None):
    for r in screen: r[:] = [' '] * 80
    pid, fd = pty.fork()
    if pid == 0:
        if env: os.environ.update(env)
        os.execv(ed, [ed, '-t', f])
    return pid, fd

def feed(txt):
    y = x = 0
    i = 0
    while i < len(txt):
        m = re.match(r'\x1b\[([0-9;?]*)([A-Za-z])', txt[i:])
        if m:
            if m.group(2) == 'H':
                a = [int(v) if v else 1 for v in (m.group(1).split(';') + ['1', '1'])[:2]]
                y, x = a[0] - 1, a[1] - 1
            elif m.group(2) == 'J':
                for r in screen: r[:] = [' '] * 80
            elif m.group(2) == 'K' and 0 <= y < 24:
                screen[y][x:] = [' '] * (80 - x)
            i += len(m.group(0)); continue
        c = txt[i]; i += 1
        if c == '\r': x = 0
        elif c == '\n': y += 1
        elif c >= ' ' and 0 <= y < 24 and 0 <= x < 80: screen[y][x] = c; x += 1
    return ''.join(screen[23]).rstrip()

def drain(fd, t=0.3):
    out = b''
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return feed(out.decode('utf-8', 'replace'))

def send(fd, s):
    os.write(fd, s.encode()); return drain(fd)

def kill(pid, fd):
    os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0); os.close(fd)

def journal():
    j = glob.glob(state + '/homeros/journal/*')
    return j[0] if j else None

def powerloss_after_save():
    """Finding 2.  journal_saved() truncates without a sync, so a power loss
    soon after :w can bring the old journal back (simulated: the journal's
    bytes from just before :w are put back after kill -9)."""
    f = d + '/f.txt'; open(f, 'w').write('one\ntwo\n')
    pid, fd = run(f); drain(fd, 0.5)
    send(fd, 'ggOtyped\x1b')
    old = open(journal(), 'rb').read()
    print('after :w      |', send(fd, ':w\r'))
    name = journal()
    kill(pid, fd)
    open(name, 'wb').write(old)
    pid, fd = run(f); print('reopen        |', drain(fd, 0.5))
    print('file          |', repr(open(f).read()))
    print(':q            |', send(fd, ':q\r'))
    print(':w            |', send(fd, ':w\r'))
    send(fd, ':q!\r'); os.waitpid(pid, 0)

def twice():
    """A second crash before any edit of the recovered buffer still recovers."""
    f = d + '/f.txt'; open(f, 'w').write('one\ntwo\n')
    pid, fd = run(f); drain(fd, 0.5)
    send(fd, 'ggOtyped\x1b'); kill(pid, fd)
    pid, fd = run(f); print('1st reopen |', drain(fd, 0.5)); kill(pid, fd)
    pid, fd = run(f); print('2nd reopen |', drain(fd, 0.5))
    send(fd, ':wq\r'); os.waitpid(pid, 0)
    print('file       |', repr(open(f).read()))

def longname():
    """Finding 4.  A hard-linked file whose path is longer than NAME_MAX as
    one name cannot be saved at all: the backup has no name."""
    p = d
    for i in range(6): p += '/' + 'd%d' % i + 'x' * 48
    os.makedirs(p)
    f = p + '/f.txt'; open(f, 'w').write('one\n'); os.link(f, f + '.link')
    print('path length', len(f))
    pid, fd = run(f); print('open  |', drain(fd, 0.5))
    send(fd, 'x')
    print(':w    |', send(fd, ':w\r'))
    print(':w!   |', send(fd, ':w!\r'))
    send(fd, ':q!\r'); os.waitpid(pid, 0)
    print('file  |', repr(open(f).read()))

def dangling():
    """A symlink to a file that does not exist yet (Neovim creates the target)."""
    f = d + '/link.txt'; os.symlink(d + '/target.txt', f)
    pid, fd = run(f); print('open |', drain(fd, 0.5))
    send(fd, 'ihello\x1b')
    print(':w   |', send(fd, ':w\r'))
    print(':w!  |', send(fd, ':w!\r'))
    send(fd, ':q!\r'); os.waitpid(pid, 0)
    print('target exists:', os.path.exists(d + '/target.txt'), '; link still a link:', os.path.islink(f))

def firstchar():
    """Finding 5.  kill -9 between a change's journal record and the record
    of the state it leaves (kill.so: SIGKILL before the KILL_AT-th writev)."""
    f = d + '/f.txt'; open(f, 'w').write('')
    pid, fd = run(f, {'LD_PRELOAD': sys.argv[3], 'KILL_AT': sys.argv[4]}); drain(fd, 0.5)
    try: send(fd, 'ihello')
    except OSError: pass
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass
    os.waitpid(pid, 0)
    print('journal bytes left:', os.path.getsize(journal()) if journal() else None)
    pid, fd = run(f); print('reopen |', drain(fd, 0.5))
    send(fd, ':wq\r'); os.waitpid(pid, 0)
    print('file   |', repr(open(f).read()))

def lateWatch():
    """Finding 6.  An outside change between file_load and file_watch (late.so
    makes it inside inotify_add_watch): no W11 at the first edit."""
    f = d + '/f.txt'; open(f, 'w').write('one\n')
    pid, fd = run(f, {'LD_PRELOAD': sys.argv[3], 'VICTIM': f}); drain(fd, 0.5)
    print('file now   |', repr(open(f).read()))
    print('first edit |', send(fd, 'x'))
    print(':w         |', send(fd, ':w\r'))
    send(fd, ':q!\r'); os.waitpid(pid, 0)

globals()[sys.argv[2]]()
shutil.rmtree(d)
