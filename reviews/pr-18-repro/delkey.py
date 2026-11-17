#!/usr/bin/env python3
"""Does the Delete key reach the editor through the terminal backend?
usage: delkey.py ./homeros
Opens a file holding "abc", presses Delete (ESC [ 3 ~, what xterm, kitty and
foot send) and then :wq, and prints the file.  "bc" means Delete worked."""
import os, pty, select, sys, tempfile, time

path = tempfile.mktemp(suffix='.txt')
open(path, 'w').write('abc\n')
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    os.execv(sys.argv[1], [sys.argv[1], path])

def drain(t):
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try: os.read(fd, 65536)
            except OSError: return

select.select([fd], [], [], 5)      # raw mode flushes input: wait for the first frame
drain(0.3)
for keys in (b'\x1b[3~', b':wq\r'):
    os.write(fd, keys); drain(0.4)
try: os.waitpid(pid, 0)
except ChildProcessError: pass
print('file after Delete and :wq: %r' % open(path).read())
os.unlink(path)
