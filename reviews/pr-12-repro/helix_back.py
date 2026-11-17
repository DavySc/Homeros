#!/usr/bin/env python3
"""Review of PR 12: where Helix's `h` lands.  Opens a file holding LINE,
goes to the last character (gl), steps left (h), deletes what the cursor is
on (d) and writes.  What is left shows how far `h` went.
usage: helix_back.py (needs hx on PATH)"""
import fcntl, os, pty, select, struct, sys, tempfile, termios, time
def run(text, keys):
    d = tempfile.mkdtemp()
    p = os.path.join(d, 'f.txt')
    open(p, 'wb').write(text + b'\n')
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM='xterm-256color', HOME=d, XDG_CONFIG_HOME=d)
        os.execvp('hx', ['hx', p])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    def pump(t):
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.05)[0]:
                try: os.read(fd, 65536)
                except OSError: return
    pump(1.5)
    os.write(fd, b'\x1b[?62c'); pump(0.5)   # answer its device-attributes query, or keys wait
    for k in b''.join(keys):
        os.write(fd, bytes([k])); pump(0.15)
    pump(0.5)
    try: os.waitpid(pid, os.WNOHANG)
    except OSError: pass
    return open(p, 'rb').read()
for name, text in [('conjunct ka+virama+ssa, x', 'क्षx'.encode()),
                   ('man+ZWJ+woman, x', '👨‍👩x'.encode()),
                   ('e+acute, x', 'éx'.encode())]:
    out = run(text, [b'gl', b'h', b'd', b':wq\r'])
    print('%-28s %r -> after gl h d: %r' % (name, text, out))
