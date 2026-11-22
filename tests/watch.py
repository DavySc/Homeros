#!/usr/bin/env python3
"""A file changed outside while the editor waits for a key, through main.c in
a pty: the first edit after it warns W11, as Neovim's buf_check_timestamp()
does, for a file given at start and for one opened from the start screen
(Find File), which buf_load watches the same way.  The loss this catches (no fd
watched, so nothing reads the change) passed every other check.
usage: watch.py HOMEROS_BINARY; exit 1 on failure"""
import os, signal, sys, tempfile
from ptyrun import spawn, drain
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp()
os.chdir(d)
bad = 0
# each case its own file and state: a killed editor's crash journal would
# make the next one's recovery warn W11 with no outside write (review of PR 120)
for how, name, args, keys in (("given at start", "a.txt", ["a.txt"], []), ("opened from the start screen", "b.txt", [], [b"f", b"b.txt", b"\r"])):
    os.environ["XDG_STATE_HOME"] = os.path.join(d, name + ".state")
    os.environ["XDG_DATA_HOME"] = os.path.join(d, name + ".data")
    open(name, "w").write("hello\n")
    pid, fd = spawn([binary, "-t"] + args)
    for k in keys + [b"j"]:   # the picker opens, lists, then takes the name
        os.write(fd, k)
        drain(fd, 0.5)
    with open(name, "a") as f: f.write("outside\n")
    drain(fd, 0.8)
    os.write(fd, b"x")
    out = drain(fd, 1.0)
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
    ok = b"W11" in out
    print("watch: a file %s, changed outside while idle: the next edit %s W11" % (how, "warns" if ok else "does NOT warn"))
    bad += not ok
sys.exit(1 if bad else 0)
