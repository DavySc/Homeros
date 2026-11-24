"""Drives the editor (or nvim) in a pty and shows the screen through pyte.
usage: drv.py ROWSxCOLS FILE step...   a step is keys (\\e Esc, \\b BS, \\r CR) or ~seconds"""
import os, pty, select, sys, time, fcntl, termios, struct, pyte
def main():
    rows, cols = map(int, sys.argv[1].split('x'))
    argv = sys.argv[2].split()
    steps = sys.argv[3:]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm-256color'; os.environ.pop('WAYLAND_DISPLAY', None)
        os.execvp(argv[0], argv)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    scr = pyte.Screen(cols, rows); st = pyte.ByteStream(scr)
    def drain(t):
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.02)[0]:
                try: st.feed(os.read(fd, 65536))
                except OSError: return
    def show(tag):
        print('--- %s (cursor row %d col %d)' % (tag, scr.cursor.y, scr.cursor.x))
        for i, l in enumerate(scr.display): print('%2d|%s' % (i, l.rstrip()))
    select.select([fd], [], [], 5); drain(0.6)
    for s in steps:
        if s.startswith('~'): drain(float(s[1:])); continue
        if s == '!': show('screen'); continue
        os.write(fd, s.encode().decode('unicode_escape').encode('latin1')); drain(0.05)
    os.kill(pid, 9)
main()
