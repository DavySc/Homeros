#!/usr/bin/env python3
"""Review of PR 136: which release's formatter :w uses, by the zig on PATH.
"a ** 2" parses before 0.17 only.  usage: pick.py HOMEROS DIR_WITH_A_ZIG (from the repo)"""
import os, sys, tempfile
sys.path.insert(0, "tests")
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
os.environ["PATH"] = os.path.abspath(sys.argv[2]) + ":/usr/bin:/bin"
d = tempfile.mkdtemp(dir="/var/tmp")
os.environ["XDG_STATE_HOME"] = os.path.join(d, "state"); os.environ["XDG_DATA_HOME"] = os.path.join(d, "data")
os.chdir(d)
src = "const a=a ** 2;\nconst E = enum { a };\nconst b=@intFromEnum(E.a);\n"
open("p.zig", "w").write(src)
pid, fd = spawn([binary, "-t", "p.zig"]); drain(fd, 1.0)
os.write(fd, b":w\r"); drain(fd, 1.5)
os.write(fd, b":q!\r"); drain(fd, 1.0)
print(sys.argv[2].split("/")[-1], "->", repr(open("p.zig").read()))
