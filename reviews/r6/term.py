import os, pty, time, select, termios, signal
def trial(how):
    open('/tmp/t.txt','w').write('x\n')
    pid, fd = pty.fork()
    if pid == 0: os.execv('./homeros', ['homeros', '/tmp/t.txt'])
    time.sleep(0.4); out = os.read(fd, 65536)
    raw = not (termios.tcgetattr(fd)[3] & termios.ICANON)
    if how == 'quit': os.write(fd, b':q!\r')
    else: os.kill(pid, how)
    time.sleep(0.4)
    try: out2 = os.read(fd, 65536) if select.select([fd],[],[],0.2)[0] else b''
    except OSError: out2 = b''
    cooked = bool(termios.tcgetattr(fd)[3] & termios.ICANON)
    os.waitpid(pid, 0)
    return raw, cooked, b'\x1b[?1049l' in out2
for how in ['quit', signal.SIGTERM, signal.SIGHUP]:
    print(how, [trial(how) for _ in range(3)], '(raw while running, cooked after, left the alternate screen)')
