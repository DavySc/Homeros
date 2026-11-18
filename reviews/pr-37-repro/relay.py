#!/usr/bin/env python3
"""Runs a command on an inner pty and passes bytes both ways, logging what the
outer terminal sent (hex, one read per line): tells a key the terminal never
sent from a key the editor lost.  usage: relay.py log cmd..."""
import os, pty, select, sys, termios, tty, fcntl, struct
log = open(sys.argv[1], 'w')
ws = fcntl.ioctl(0, termios.TIOCGWINSZ, b'\0' * 8)
pid, fd = pty.fork()
if pid == 0: os.execvp(sys.argv[2], sys.argv[2:])
fcntl.ioctl(fd, termios.TIOCSWINSZ, ws)
old = termios.tcgetattr(0); tty.setraw(0)
try:
    while True:
        r = select.select([0, fd], [], [])[0]
        if 0 in r:
            b = os.read(0, 4096); log.write(b.hex() + '\n'); log.flush(); os.write(fd, b)
        if fd in r:
            try: b = os.read(fd, 65536)
            except OSError: break
            if not b: break
            os.write(1, b)
finally:
    termios.tcsetattr(0, termios.TCSANOW, old)
