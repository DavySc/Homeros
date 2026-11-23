#!/usr/bin/env python3
"""Review of PR 136, in the editor itself (a pty, homeros -t).
 1. typing "?" at the top of an empty a.zig: the editor must still be running;
 2. with no zig on PATH, :w of a Zig 0.16 file that uses @intFromEnum: what is on disk.
usage: editor.py HOMEROS_BINARY DIR_WITH_ZIG_OR_EMPTY (run from the repo, for tests/ptyrun.py)"""
import os, signal, sys, tempfile, time
sys.path.insert(0, "tests")
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(dir="/var/tmp")
os.environ["XDG_STATE_HOME"] = os.path.join(d, "state")
os.environ["XDG_DATA_HOME"] = os.path.join(d, "data")
os.chdir(d)
def alive(pid):
    done, st = os.waitpid(pid, os.WNOHANG)
    return None if not done else os.waitstatus_to_exitcode(st)
# 1
open("a.zig", "w").write("")
pid, fd = spawn([binary, "-t", "a.zig"])
drain(fd, 1.0)
os.write(fd, b"i?"); drain(fd, 1.5)
st = alive(pid)
print('1. "?" typed at the top of an empty a.zig: %s' % ("the editor is still running" if st is None else "the editor died, status %d (%s)" % (st, signal.Signals(-st).name if st < 0 else "exit")))
if st is None: os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
# 2
os.environ["PATH"] = "/usr/bin:/bin"
src = "const E = enum { a };\ntest {\n    _ = @intFromEnum(E.a);\n}\n"
open("e.zig", "w").write(src)
pid, fd = spawn([binary, "-t", "e.zig"])
drain(fd, 1.0)
os.write(fd, b":w\r"); drain(fd, 1.5)
os.write(fd, b":q!\r"); drain(fd, 1.0)
now = open("e.zig").read()
print("2. no zig on PATH, :w of a Zig 0.16 file: %s" % ("left as typed" if now == src else "rewritten on disk to:\n" + now))
