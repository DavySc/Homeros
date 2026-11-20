#!/usr/bin/env python3
"""PR 86 review probes: q after :Dashboard with an unsaved file; the dashboard
after CTRL-^ back to the file.  usage: probe.py HOMEROS [N]"""
import os, signal, sys, tempfile, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), "tests"))
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(dir="/var/tmp")
os.environ["XDG_STATE_HOME"] = d + "/state"; os.environ["XDG_DATA_HOME"] = d + "/data"
os.chdir(d)
def alive(pid, t=1.5):
    end = time.time() + t
    while time.time() < end:
        done, st = os.waitpid(pid, os.WNOHANG)
        if done: return False
        time.sleep(0.05)
    return True
def case(name, keys_before, final):
    open("alpha.txt", "w").write("alpha\n")
    pid, fd = spawn([binary, "-t", "alpha.txt"])
    drain(fd, 0.3)
    for k in keys_before: os.write(fd, k); drain(fd, 0.4)
    screen = b""
    os.write(fd, final); screen = drain(fd, 0.6)
    up = alive(pid)
    data = open("alpha.txt").read()
    if up: os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
    return up, data, screen
# A: unsaved change, :Dashboard, q  (nvim + snacks: :qa refuses, E37/E162)
up, data, s = case("A", [b"Ax", b"\x1b", b":Dashboard\r"], b"q")
print("A unsaved+:Dashboard+q: still running=%s file=%r (want running, E37/E162)" % (up, data))
# B: :Dashboard, CTRL-^ back to alpha.txt, then q  (snacks: the dashboard is gone, q is the q command)
up, data, s = case("B", [b":Dashboard\r", b"\x1e"], b"")
print("B :Dashboard + CTRL-^: dashboard still drawn=%s" % (b"Recent Files" in s or b"h o m e r o s" in s))
up, data, s = case("B2", [b":Dashboard\r", b"\x1e"], b"q")
print("B2 :Dashboard + CTRL-^ + q: still running=%s (want running: q starts a recording)" % up)
open("alpha.txt", "w").write("alpha\n")
pid, fd = spawn([binary, "-t", "alpha.txt"]); drain(fd, 0.3)
os.write(fd, b":Dashboard\r"); drain(fd, 0.5)
os.write(fd, b"\x1e"); s = drain(fd, 0.6)
print("B3 after CTRL-^ the screen writes: alpha=%s, start screen=%s" % (b"alpha" in s, b"Find File" in s or b"o m e r" in s))
os.write(fd, b":ls\r"); s = drain(fd, 0.6); print("   :ls ->", s[-300:])
os.kill(pid, signal.SIGKILL)
