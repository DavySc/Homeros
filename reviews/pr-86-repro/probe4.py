#!/usr/bin/env python3
"""Two buffers on one file (after :Dashboard r Enter): change and write
buffer 2, switch to buffer 1 (SPC , then 1), change and write it.  Prints
the file and the messages.  usage: probe4.py HOMEROS"""
import os, signal, sys, tempfile, pyte
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), "tests"))
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(dir="/var/tmp")
os.environ["XDG_STATE_HOME"] = d + "/state"; os.environ["XDG_DATA_HOME"] = d + "/data"
os.chdir(d)
open("alpha.txt", "w").write("alpha line\n")
scr = pyte.Screen(80, 24); st = pyte.ByteStream(scr)
pid, fd = spawn([binary, "-t", "alpha.txt"]); st.feed(drain(fd, 0.4))
for k in [b":Dashboard\r", b"r", b"\r", b"Abee", b"\x1b", b":w\r"]: os.write(fd, k); st.feed(drain(fd, 0.5))
print("after 2nd :w:", scr.display[-1].strip(), "| file:", repr(open("alpha.txt").read()))
for k in [b" ,", b"\x0e", b"\r"]: os.write(fd, k); st.feed(drain(fd, 0.5))
for k in [b"Acat", b"\x1b", b":w\r"]: os.write(fd, k); st.feed(drain(fd, 0.5))
print("after 1st :w:", scr.display[-1].strip(), "| file:", repr(open("alpha.txt").read()))
os.kill(pid, signal.SIGKILL)
