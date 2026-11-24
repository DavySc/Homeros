"""PR 129 second review: Space in a 3-row terminal.  usage: tiny.py HOMEROS FILE"""
import os, pty, select, sys, time, fcntl, termios, struct
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'; os.environ.pop('WAYLAND_DISPLAY', None)
    os.execv(sys.argv[1], [sys.argv[1], '-t', sys.argv[2]])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 3, 80, 0, 0))
out = b''
def drain(t):
    global out
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try: out += os.read(fd, 65536)
            except OSError: return
select.select([fd], [], [], 5); drain(0.8)
os.write(fd, b' '); drain(1.0)
p, st = os.waitpid(pid, os.WNOHANG)
print('exited: status %d' % st if p else 'still running')
i = out.find(b'ERROR: AddressSanitizer')
if i >= 0: print(out[i:i+400].decode(errors='replace'))
if not p: os.kill(pid, 9)
