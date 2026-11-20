#!/usr/bin/env python3
"""Review of PR 59: the real editor (build/san/homeros -t, ASan) in a
terminal of ROWS x COLS; types :w<CR>, waits for the kitten, prints what the
editor wrote to stderr and its exit status.  usage: small.py ROWS COLS"""
import fcntl, os, pty, select, struct, sys, tempfile, termios, time
rows, cols = int(sys.argv[1]), int(sys.argv[2])
d = tempfile.mkdtemp()
err = os.path.join(d, 'err')
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    os.dup2(os.open(err, os.O_WRONLY | os.O_CREAT), 2)
    os.execv('build/san/homeros', ['homeros', '-t', os.path.join(d, 'notes.org')])
seen = b''
def drain(t):
    global seen
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try: seen += os.read(fd, 65536)
            except OSError: return
drain(1.0)                      # its first output, then keys
os.write(fd, b':w\r')
drain(3.0)
try: os.write(fd, b':q\r')
except OSError: pass
drain(0.5)
_, st = os.waitpid(pid, 0)
print('%dx%d: exit %s; kitten drawn: %s; stderr: %s' % (rows, cols, os.waitstatus_to_exitcode(st), b'^.^' in seen, (open(err).read().splitlines() or ['(nothing)'])[:6]))
