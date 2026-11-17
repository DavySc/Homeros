"""Drive ./homeros in a pty.  usage: pty.py HOMEROS FILE CASE
split: 'a' + 200 'é' typed in one burst (401 bytes; the tty read buffer is 256)
resize: 300 'a' plus a window resize, both pending before the editor reads"""
import os, pty, signal, sys, time, fcntl, termios, struct
exe, path, case = sys.argv[1:4]
pid, fd = pty.fork()
if pid == 0:
    os.execv(exe, [exe, path])
def drain():
    time.sleep(0.3)
    try:
        while True:
            import select
            if not select.select([fd], [], [], 0.05)[0]: break
            os.read(fd, 65536)
    except OSError: pass
drain()
os.write(fd, b'i'); drain()
os.kill(pid, signal.SIGSTOP); time.sleep(0.1)
if case == 'split': os.write(fd, b'a' + 'é'.encode() * 200)
else:
    os.write(fd, b'a' * 300)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))   # sends SIGWINCH
time.sleep(0.1)
os.kill(pid, signal.SIGCONT); drain()
os.write(fd, b'\x1b'); drain()
os.write(fd, b':wq\r'); drain()
os.waitpid(pid, 0)
s = open(path, encoding='utf-8').read().rstrip('\n')
if case == 'split':
    print('typed a + 200 é; file has %d chars, %d é; odd ones: %r' % (len(s), s.count('é'), sorted(set(s) - set('aé'))))
else:
    print('typed 300 a; file has %d' % s.count('a'))
