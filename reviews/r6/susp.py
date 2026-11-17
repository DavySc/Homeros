# Ctrl-Z, the terminal resized while the editor is stopped and the shell is in the foreground, then fg.
import os, pty, sys, time, select, fcntl, termios, struct, signal, re
EXE = sys.argv[1]; ARGS = sys.argv[2:]
def trial(stop):
    open('/tmp/s.txt','w').write(''.join('line %d\n' % i for i in range(60)))
    pid, fd = pty.fork()
    if pid == 0:                      # the "shell": session leader on the pty
        signal.signal(signal.SIGTTOU, signal.SIG_IGN)
        ed = os.fork()
        if ed == 0:
            os.setpgid(0, 0); os.tcsetpgrp(0, os.getpid()); signal.signal(signal.SIGTTOU, signal.SIG_DFL)
            os.execv(EXE, [EXE] + ARGS + ['/tmp/s.txt'])
        try: os.setpgid(ed, ed)
        except OSError: pass
        while True:
            _, st = os.waitpid(ed, os.WUNTRACED)
            if os.WIFSTOPPED(st):
                os.tcsetpgrp(0, os.getpgrp())          # the shell takes the terminal
                time.sleep(1.0)                        # the user resizes the window meanwhile
                os.tcsetpgrp(0, ed); os.kill(-ed, signal.SIGCONT)   # fg
            else: os._exit(0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    out = b''
    def pump(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.05)[0]:
                try: out += os.read(fd, 65536)
                except OSError: return
    pump(0.7)
    if stop: os.write(fd, b'\x1a'); pump(0.4)
    mark = len(out)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 100, 0, 0))
    pump(2.0)
    rows = [int(m) for m in re.findall(rb'\x1b\[(\d+);\d+H', out[mark:])]
    os.write(fd, b':q!\r'); pump(0.5)
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass
    os.waitpid(pid, 0)
    return max(rows) if rows else 0
for i in range(3):
    print('resize while running: lowest row drawn', trial(0), '| resize while stopped, then fg: lowest row drawn', trial(1))
