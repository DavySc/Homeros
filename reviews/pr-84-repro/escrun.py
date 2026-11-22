# Opens FILE in the editor in a pty, waits for the syntax pass, quits, and
# reports whether the file's own escape sequence reached the terminal.
import os, pty, sys, time, select
exe, f = sys.argv[1], sys.argv[2]
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    os.environ.pop('WAYLAND_DISPLAY', None)
    os.execv(exe, [exe, f])
import fcntl, termios, struct
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 20, 60, 0, 0))
out = b''
def rd(t):
    global out
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: return
rd(2.0)
os.write(fd, b'j'); rd(0.5); os.write(fd, b'G'); rd(1.5)
os.write(fd, b':q!\r'); rd(1.0)
open(f + '.out', 'wb').write(out)
print('bytes', len(out), 'raw OSC from file:', b'\x1b]0;PWNED\x07' in out, 'raw tab after it:', b'PWNED\x07\t' in out)
i = out.find(b'PWNED')
print(repr(out[max(0, i - 40):i + 30]) if i >= 0 else 'PWNED not drawn')
