#!/usr/bin/env python3
"""Review of PR 120: tests/watch.py's second case (Find File from the start
screen), with and without the outside write, printing what the editor shows.
usage: watch_case2.py HOMEROS_BINARY"""
import os, re, signal, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tests"))
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp()
os.environ["XDG_STATE_HOME"] = d + "/state"; os.environ["XDG_DATA_HOME"] = d + "/data"
os.chdir(d)
for write in 1, 0:
    open("a.txt", "w").write("hello\n")
    pid, fd = spawn([binary, "-t"])
    for k in [b"f", b"a.txt", b"\r", b"j"]:
        os.write(fd, k); drain(fd, 0.5)
    if write:
        with open("a.txt", "a") as f: f.write("outside\n")
    drain(fd, 0.8)
    os.write(fd, b"x")
    out = drain(fd, 1.0)
    os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
    m = re.search(rb"W11[^\x1b]*", out)
    print("outside write: %s; after x: %s" % ("yes" if write else "no", m.group(0).decode() if m else "no W11"))
