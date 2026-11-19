import os, pty, select, sys, time
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    os.execvp('nvim', ['nvim', '-u', 'NONE', '-n', '-i', 'sh.shada', sys.argv[1]])
def drain(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try: os.read(fd, 1 << 16)
            except OSError: return
drain(0.5); os.write(fd, b':qa\r'); drain(0.4); os.waitpid(pid, 0)
