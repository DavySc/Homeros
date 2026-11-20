#!/usr/bin/env python3
"""Pre-existing (core): one file under two names makes two buffers.  homeros
alpha.txt, then SPC f r (recent, full path) Enter, then SPC , lists the
buffers.  Neovim (buflist_findname by full name and inode) has one.
usage: probe3.py HOMEROS"""
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
for k in [b":Dashboard\r", b"r", b"\r", b" ,"]: os.write(fd, k); st.feed(drain(fd, 0.5))
print("\n".join(r.rstrip() for r in scr.display if r.strip()))
os.kill(pid, signal.SIGKILL)
