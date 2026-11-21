import os, pty, sys, time, select, re
def run(path, keys=b''):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm-256color'; os.environ.pop('WAYLAND_DISPLAY', None)
        os.execv('/home/claude/probe/homeros', ['homeros', path])
    import fcntl, termios, struct
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    out = b''
    def rd(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: out += os.read(fd, 65536)
                except OSError: return
    rd(1.0)
    for k in keys:
        os.write(fd, bytes([k])); rd(0.15)
    rd(0.5)
    mark = len(out)
    os.write(fd, b'\x1b:q!\r'); rd(0.5)
    try: os.kill(pid, 9)
    except OSError: pass
    os.waitpid(pid, 0)
    return out[:mark]
for p in sys.argv[1:]:
    o = run(p, b'')
    print(p, len(o), 'sgr colours:', sorted(set(re.findall(rb'\x1b\[[0-9;]*m', o)))[:40])
