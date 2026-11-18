#!/usr/bin/env python3
"""git.h: main.c gives every key to git_key before keys_feed.  The keys
package's pending state (a count, an operator) is its own, so a key git
takes leaves it standing, and it lands on the next Vim key.
usage: pending.py ./homeros-with-fake-git
The editor is built with fake_git.c, whose git_key takes x.  On a file of 9
lines this types 3 x dd :wq.  Had x been Vim's (3x), dd would delete one
line; here the 3 waits and dd deletes three."""
import os, pty, select, sys, tempfile, time

path = tempfile.mktemp(suffix='.txt')
open(path, 'w').write(''.join('line %d\n' % i for i in range(1, 10)))
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

select.select([fd], [], [], 5)
drain(0.3)
for keys in (b'3', b'x', b'dd', b':wq\r'):
    os.write(fd, keys); drain(0.3)
try: os.waitpid(pid, 0)
except ChildProcessError: pass
left = open(path).read().split('\n')[:-1]
print('typed 3 x dd: %d of 9 lines left, the first is %r' % (len(left), left[0]))
os.unlink(path)
