#!/usr/bin/env python3
"""PR 86 review probe: :Dashboard, then SPC , (buffers picker) Enter on
alpha.txt, or the dashboard's r Enter: the window shows alpha.txt again, so
the start screen must be gone and q must not quit.  Renders with pyte.
usage: probe2.py HOMEROS"""
import os, signal, sys, tempfile, time, pyte
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), "tests"))
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(dir="/var/tmp")
os.environ["XDG_STATE_HOME"] = d + "/state"; os.environ["XDG_DATA_HOME"] = d + "/data"
os.chdir(d)
def run(name, keys):
    open("alpha.txt", "w").write("alpha line\n")
    scr = pyte.Screen(80, 24); st = pyte.ByteStream(scr)
    pid, fd = spawn([binary, "-t", "alpha.txt"]); st.feed(drain(fd, 0.4))
    for k in keys: os.write(fd, k); st.feed(drain(fd, 0.5))
    os.write(fd, b":w\r"); st.feed(drain(fd, 0.5))
    rows = scr.display
    print("  :w says:", rows[-1].strip())
    dash = any("Find File" in r for r in rows)
    alpha = any("alpha line" in r for r in rows)
    os.write(fd, b"q"); drain(fd, 0.5)
    time.sleep(0.5)
    done, _ = os.waitpid(pid, os.WNOHANG)
    if not done: os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
    print("%s: start screen drawn=%s, alpha.txt visible=%s, q quit=%s" % (name, dash, alpha, bool(done)))
    if dash: print("\n".join(r.rstrip() for r in rows if r.strip()))
run("SPC , Enter", [b":Dashboard\r", b" ,", b"alpha", b"\r"])
run("r Enter", [b":Dashboard\r", b"r", b"\r"])
