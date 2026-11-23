#!/usr/bin/env python3
"""Records what a program writes to a terminal of ROWS x COLS (default 43 x 178,
the terminal float of a 48 x 198 editor), sending each key of KEYS
(separated by spaces, Python escapes; w alone is a wait) 0.3 s apart.
Usage: record.py OUT 'command' 'keys' [ROWS COLS]"""
import fcntl, os, pty, select, struct, sys, termios, time
out, cmd, keys = sys.argv[1], sys.argv[2], sys.argv[3]
rows, cols = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (43, 178)
pid, fd = pty.fork()
if pid == 0:
    os.environ.update(TERM='xterm-256color', COLORTERM='truecolor', LINES=str(rows), COLUMNS=str(cols))
    os.execvp('sh', ['sh', '-c', cmd])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
got = bytearray()
def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], max(0, end - time.time()))
        if not r: break
        try: b = os.read(fd, 65536)
        except OSError: return False
        if not b: return False
        got.extend(b)
    return True
pump(1.5)
for k in keys.split(' ') if keys else []:
    if k != 'w': os.write(fd, k.encode().decode('unicode_escape').encode())
    if not pump(0.3): break
try: os.kill(pid, 9)
except OSError: pass
open(out, 'wb').write(got)
