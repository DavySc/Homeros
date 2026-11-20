"""PR 89 review: the start screen animation on the real loop, through a pty.
usage: e2e.py HOMEROS full|close|reopen
Prints output bursts (frames) and the editor's voluntary context switches
(its wakes) over idle stretches."""
import os, pty, sys, time, select, fcntl, termios, struct, tempfile, re
exe, mode = sys.argv[1], sys.argv[2]
tmp = tempfile.mkdtemp()
pid, fd = pty.fork()
if pid == 0:
    for k in ('WAYLAND_DISPLAY', 'DISPLAY'): os.environ.pop(k, None)
    os.environ.update(TERM='xterm-256color', COLORTERM='truecolor', HOME=tmp, XDG_STATE_HOME=tmp, XDG_DATA_HOME=tmp, XDG_CONFIG_HOME=tmp)
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 100, 0, 0))
    os.chdir(tmp)
    os.execv(exe, [exe])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 100, 0, 0))
t0 = time.monotonic()
bursts = []
def wakes():
    s = open('/proc/%d/status' % pid).read()
    return int(re.search(r'^voluntary_ctxt_switches:\s+(\d+)', s, re.M).group(1))
def run(sec):
    """read for sec seconds; return the bursts' times"""
    end, got = time.monotonic() + sec, []
    while True:
        left = end - time.monotonic()
        if left <= 0: return got
        if select.select([fd], [], [], left)[0]:
            d = os.read(fd, 65536)
            got.append((time.monotonic() - t0, len(d)))
def gaps(b): return sorted(round((y[0] - x[0]) * 1000) for x, y in zip(b, b[1:]))
def report(name, b, w0, w1):
    g = gaps(b)
    print('%s: %d bursts, first %.2f s last %.2f s, gap median %s ms (min %s, max %s), wakes %d' % (
        name, len(b), b[0][0] if b else -1, b[-1][0] if b else -1,
        g[len(g) // 2] if g else '-', g[0] if g else '-', g[-1] if g else '-', w1 - w0))
first = run(1.0)   # wait for the first draw before any key
if mode == 'full':
    w = wakes(); b = run(299.5); report('0 to 300.5 s', first + b, w, wakes())
    w = wakes(); b = run(20); report('300.5 to 320.5 s', b, w, wakes())
elif mode == 'close':
    w = wakes(); b = run(5); report('open, 5 s', b, w, wakes())
    os.write(fd, b'i'); run(0.5)
    w = wakes(); b = run(10); report('closed by i, 10 s', b, w, wakes())
elif mode == 'reopen':
    os.write(fd, b'i'); run(0.5); os.write(fd, b'\x1b'); run(0.5)
    w = wakes(); b = run(5); report('closed, 5 s', b, w, wakes())
    os.write(fd, b':Dashboard\r'); run(0.3)
    w = wakes(); b = run(5); report('after :Dashboard, 5 s', b, w, wakes())
    os.write(fd, b':Dashboard\r'); run(0.3)
    w = wakes(); b = run(5); report('after a second :Dashboard, 5 s', b, w, wakes())
os.kill(pid, 9); os.waitpid(pid, 0)
