#!/usr/bin/env python3
"""The start screen and the recent files through main.c, in a pty: a session
on alpha.txt opens the start screen with :Dashboard and SPC D (with the
startup time) and quits with its q (main.c writes v:oldfiles at exit); then
homeros with no file shows the start screen, whose r lists alpha.txt (v:oldfiles read at
startup).  usage: start.py HOMEROS_BINARY; exit 1 on failure"""
import os, re, signal, sys, tempfile, time
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(dir="/var/tmp")   # not under /tmp/, which v:oldfiles leaves out
os.environ["XDG_STATE_HOME"] = os.path.join(d, "state")
os.environ["XDG_DATA_HOME"] = os.path.join(d, "data")
os.chdir(d)
open("alpha.txt", "w").write("alpha\n")
def run(args): return spawn([binary, "-t"] + args)
def quit(pid, fd, keys):
    """exit status, or -9 (killed) when the keys leave it running 3 s"""
    os.write(fd, keys); drain(fd, 1.0)
    end = time.time() + 3
    while True:
        done, st = os.waitpid(pid, os.WNOHANG)
        if done: return os.waitstatus_to_exitcode(st)
        if time.time() > end: os.kill(pid, signal.SIGKILL)
        time.sleep(0.05)
pid, fd = run(["alpha.txt"])
os.write(fd, b":Dashboard\r")
cmd = drain(fd, 1.0)
os.write(fd, b"n\x1b")   # n: Insert on the empty buffer, gone
drain(fd, 0.3)            # an Esc and a key at once are Alt-key
os.write(fd, b" D")
spc = drain(fd, 1.0)
rc = quit(pid, fd, b"q")
for what, out in ((":Dashboard", cmd), ("SPC D", spc)):
    # the number has its own colour: escapes between "in " and it
    if b"Recent Files" not in out or re.search(rb"loaded in (\x1b\[[0-9;:]*m)*0\.0ms", out):
        print("FAIL start: %s on a file session %s" % (what,
              "showed no start screen" if b"Recent Files" not in out else "lost the startup time")); sys.exit(1)
if rc:
    print("FAIL start: the first session's q exited %d" % rc); sys.exit(1)
pid, fd = run([])
screen = drain(fd, 1.0)
os.write(fd, b"r")
picker = drain(fd, 1.0)
rc = quit(pid, fd, b"\x1b\x1bq")
ok = b"Recent Files" in screen and b"alpha.txt" in picker and not rc
print("start: with no file the start screen %s; its recent picker %s alpha.txt" %
      ("shows" if b"Recent Files" in screen else "is MISSING", "lists" if b"alpha.txt" in picker else "does NOT list"))
if rc: print("FAIL start: the start screen's q left homeros running")
sys.exit(0 if ok else 1)
