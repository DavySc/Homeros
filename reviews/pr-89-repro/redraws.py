"""PR 89 review: j and k every 100 ms redraw the start screen; the animation
must go on.  The whiskers' "~" is written at frames 4 and 13 (1.4 s, 4.55 s);
after the first draw no other "~" is written.
usage: redraws.py HOMEROS"""
import os, pty, sys, time, select, fcntl, termios, struct, tempfile
exe = sys.argv[1]
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
def run(sec):
    end, got = time.monotonic() + sec, []
    while True:
        left = end - time.monotonic()
        if left <= 0: return got
        if select.select([fd], [], [], left)[0]:
            got.append((time.monotonic() - t0, os.read(fd, 65536)))
run(0.5)
got, keys = [], 0
while time.monotonic() - t0 < 6.0:
    os.write(fd, b'jk'[keys % 2:keys % 2 + 1]); keys += 1
    got += run(0.1)
print('%d keys, %d bursts; "~" written at %s s' % (keys, len(got), ['%.2f' % t for t, d in got if b'~' in d]))
os.kill(pid, 9); os.waitpid(pid, 0)
