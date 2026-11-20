#!/usr/bin/env python3
"""Type into the editor through a pty, then hang the terminal up: the editor
must exit within 2 s (not spin) and keep its crash journal, so the next open
recovers the text, as Neovim keeps its swap file on SIGHUP (package 8's
review, finding 4).  usage: hup.py HOMEROS_BINARY; exit 1 on failure"""
import os, sys, time, tempfile, signal
from ptyrun import spawn, drain
binary = sys.argv[1]
d = tempfile.mkdtemp(); path = os.path.join(d, "f")
os.environ["XDG_STATE_HOME"] = os.path.join(d, "state")
open(path, "w").close()
def run(): return spawn([binary, "-t", path])
pid, fd = run()
os.write(fd, b"ihello\x1b"); drain(fd, 1.0)
os.close(fd)                       # the hang-up
end = time.time() + 2; st = None
while time.time() < end and st is None:
    p, s = os.waitpid(pid, os.WNOHANG)
    if p: st = s
    else: time.sleep(0.05)
if st is None:
    os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
    print("FAIL hup: still running 2 s after the hang-up"); sys.exit(1)
pid, fd = run()
os.write(fd, b":wq\r"); drain(fd, 1.0); os.waitpid(pid, 0)
got = open(path).read()
print("hang-up: exit status %d; after reopening, the file holds %r" % (os.waitstatus_to_exitcode(st), got))
sys.exit(0 if got == "hello\n" else 1)
