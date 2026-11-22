#!/usr/bin/env python3
"""Review of PR 105: the real editor (-t, a pty) on an Ibex file with
slang-server on PATH: type a syntax error, :w, and print what the screen
gained.  usage: e2e_sv.py HOMEROS FILE.sv (run inside the project)"""
import os, pty, sys, time, select, re, fcntl, termios, struct
exe, f = sys.argv[1], sys.argv[2]
src = open(f, "rb").read()
pid, fd = pty.fork()
if pid == 0:
    os.execve(exe, [exe, "-t", f], dict(os.environ, TERM="xterm-256color"))
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))
def pump(sec):
    end = time.time() + sec; out = b""
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out
def plain(b): return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]|\x1b[=>]", "\n", b.decode("utf-8", "replace"))
pump(3.0)
os.write(fd, b"20GO@@@ \x1b"); a = pump(0.2)
t0 = time.time(); os.write(fd, b":w\r")
out = b""; shown = None
while time.time() - t0 < 3:
    out += pump(0.005)
    if shown is None and re.search(r"expected|unknown|error|@@@.*\S", plain(out).split(":w")[-1]) and b"expected" in out: shown = time.time() - t0
lines = [l.strip() for l in plain(out).split("\n") if re.search(r"expect|error|E\d+|unknown|slang", l)]
print("after :w, new on screen:", lines[:6]); print("first seen after %s ms" % (None if shown is None else round(shown * 1000)))
import subprocess; print("slang-server children of the editor:", subprocess.run(["pgrep","-P",str(pid),"-l"],capture_output=True,text=True).stdout.split()); print("screen after :w mentions the typed line:", "@@@" in plain(a+out)); print("status line:", [l for l in plain(out).split("\n") if "ibex_alu" in l][-1:])
os.write(fd, b"u:w\r:q!\r"); pump(1.0)
try: os.kill(pid, 9)
except OSError: pass
open(f, "wb").write(src)
