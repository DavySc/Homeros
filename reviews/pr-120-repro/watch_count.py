#!/usr/bin/env python3
"""Review of PR 120: how often does the loop call core_watch for ONE outside
write to the file given at start?  Needs the
editor built with watch-count.diff (core_watch appends a line to $CW_LOG).
usage: watch_count.py HOMEROS_BINARY"""
import os, signal, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tests"))
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp()
os.environ["XDG_STATE_HOME"] = d + "/state"; os.environ["XDG_DATA_HOME"] = d + "/data"
os.environ["CW_LOG"] = d + "/log"
os.chdir(d)
def calls(): return len(open(d + "/log").readlines()) if os.path.exists(d + "/log") else 0
for name in "a.txt", "b.txt": open(name, "w").write("hello\n")
pid, fd = spawn([binary, "-t", "a.txt"])
drain(fd, 0.5)
n0 = calls()
with open("a.txt", "a") as f: f.write("outside\n")
drain(fd, 0.8)
n1 = calls()
print("file given at start: %d core_watch calls for one outside write (at load: %d)" % (n1 - n0, n0))
os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
