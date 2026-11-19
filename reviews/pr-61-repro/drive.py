#!/usr/bin/env python3
"""drive.py COLSxROWS KEYS [FILE]: runs homeros -t in a pty, feeds KEYS (python-escaped), prints the screen (pyte) and exit status."""
import fcntl, os, pty, select, struct, sys, termios, time
sys.path.insert(0, '/home/claude/Homeros/tools-env/lib/python3.14/site-packages')
import pyte, pyte.screens
_w = pyte.screens.wcwidth
pyte.screens.wcwidth = lambda ch: 1 if 0xE000 <= ord(ch) <= 0xF8FF else _w(ch)
cols, rows = map(int, sys.argv[1].split('x'))
keys = sys.argv[2].encode().decode('unicode_escape').encode('latin1')
argv = [os.environ.get('HOMEROS', './homeros'), '-t'] + sys.argv[3:]
pid, fd = pty.fork()
if pid == 0:
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    os.execvp(argv[0], argv)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
scr = pyte.Screen(cols, rows); st = pyte.ByteStream(scr)
raw = b''
def drain(t):
    global raw
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                d = os.read(fd, 1 << 16); raw += d; st.feed(d)
            except OSError: return
drain(0.8)
for k in keys.split(b'|'):
    os.write(fd, k); drain(0.4)
for y in range(rows): print('|' + ''.join(scr.buffer[y][x].data for x in range(cols)).rstrip() + '|')
print('cursor', scr.cursor.y, scr.cursor.x)
try:
    p, s = os.waitpid(pid, os.WNOHANG)
    print('exited' if p else 'running', s)
    if not p: os.kill(pid, 9)
except Exception as e: print(e)
if os.environ.get('RAW'): open(os.environ['RAW'], 'wb').write(raw)
