#!/usr/bin/env python3
"""Drive ./homeros on a pty and print the screen (review of PR 29).
usage: pty.py file step...   a step is keys (with \\x1b etc. escapes), or
  @shell command, or  =  (print screen), or ~secs (wait)"""
import os, pty, sys, time, select, subprocess, pyte, codecs
file, steps = sys.argv[1], sys.argv[2:]
screen = pyte.Screen(100, 30); stream = pyte.ByteStream(screen)
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    import fcntl, termios, struct
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
    os.execv(os.environ.get('HOMEROS', './homeros'), ['homeros', file])
def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: d = os.read(fd, 65536)
            except OSError: return False
            if not d: return False
            stream.feed(d)
    return True
pump(0.8)
for s in steps:
    if s == '=':
        print('-' * 40)
        for i, l in enumerate(screen.display):
            if l.strip(): print('%2d|%s' % (i, l.rstrip()))
        print('cursor', screen.cursor.y, screen.cursor.x)
    elif s.startswith('@'): subprocess.run(s[1:], shell=True)
    elif s.startswith('~'): pump(float(s[1:]))
    else:
        for ch in codecs.decode(s, 'unicode_escape').encode('latin1'):
            os.write(fd, bytes([ch])); pump(0.03)
        if not pump(0.5): print('EDITOR EXITED'); break
try:
    os.kill(pid, 9)
except OSError: pass
_, st = os.waitpid(pid, 0)
