import os, pty, sys, time, select, fcntl, termios, struct, signal
def run(path, steps, rows=24, cols=80, exe='./homeros'):
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(exe, [exe, path])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    out = b''
    def pump(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: d = os.read(fd, 65536)
                except OSError: return False
                if not d: return False
                out += d
        return True
    pump(0.3)
    for s in steps:
        if callable(s): s(pid, fd)
        elif isinstance(s, float): pump(s)
        else: os.write(fd, s); pump(0.2)
    pump(0.3)
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass
    os.waitpid(pid, 0)
    return out
