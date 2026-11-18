#!/usr/bin/env python3
"""Review of PR 59: system calls of the real editor (build/san/homeros -t) in
a 24 x 80 terminal.  It types :w<CR>; strace then counts every system call in
(a) the 2.2 s the kitten plays and (b) 10 s after it has ended.
usage: idle.py   (prints the two counts)"""
import fcntl, os, pty, select, struct, subprocess, sys, tempfile, termios, time
d = tempfile.mkdtemp()
logs = tempfile.mkdtemp()   # not in d: the editor watches its file's directory
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    os.dup2(os.open(os.devnull, os.O_WRONLY), 2)
    os.execv('build/san/homeros', ['homeros', '-t', os.path.join(d, 'notes.org')])
def drain(t):
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]: os.read(fd, 65536)
def trace(t):
    log = os.path.join(logs, 'st')
    p = subprocess.Popen(['strace', '-qq', '-o', log, '-p', str(pid)])
    drain(t)
    p.terminate(); p.wait()
    return [l.split('(')[0] for l in open(log) if '(' in l and 'resumed' not in l]
drain(1.0)
p = subprocess.Popen(['strace', '-qq', '-o', os.path.join(logs, 'st0'), '-p', str(pid)])
time.sleep(0.5)
os.write(fd, b':w\r')
drain(3.5)
p.terminate(); p.wait()
play = [l.split('(')[0] for l in open(os.path.join(logs, 'st0')) if '(' in l and 'resumed' not in l]
import collections
print('from :w for 4 s: %s' % dict(collections.Counter(play)))
idle = trace(10)
print('10 s after it ended: %d calls %s' % (len(idle), idle[:5]))
os.kill(pid, 9)
