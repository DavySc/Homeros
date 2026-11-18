#!/usr/bin/env python3
"""Runs inside a terminal: raw mode, prints READY, then writes every byte the
terminal sends within the next seconds to a file as hex, one read per line.
usage: rawcap.py out.txt [seconds]"""
import os, select, sys, termios, time, tty
old = termios.tcgetattr(0)
tty.setraw(0)
out = open(sys.argv[1], 'w')
os.write(1, b'READY\r\n')
end = time.time() + float(sys.argv[2] if len(sys.argv) > 2 else 6)
while time.time() < end:
    if select.select([0], [], [], 0.05)[0]:
        out.write(os.read(0, 4096).hex() + '\n'); out.flush()
termios.tcsetattr(0, termios.TCSANOW, old)
