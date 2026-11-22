#!/usr/bin/env python3
"""Review of PR 105: a hanging verible-verilog-format under the real editor
(-t, a pty).  usage: hang.py HOMEROS ctrlc|wait
ctrlc: Ctrl-C 300 ms into :Format must give the editor back at once;
wait: keys typed during the 1 s wait must run after it, none lost."""
import os, pty, sys, time, select, tempfile, subprocess
exe, mode = sys.argv[1], sys.argv[2]
d = tempfile.mkdtemp()
os.mkdir(d + "/bin")
with open(d + "/bin/verible-verilog-format", "w") as f: f.write("#!/bin/sh\nexec sleep 10\n")
os.chmod(d + "/bin/verible-verilog-format", 0o700)
src = "module m;\n  wire a;\nendmodule\n"
open(d + "/m.sv", "w").write(src)
env = dict(os.environ, PATH=d + "/bin:" + os.environ["PATH"], HOME=d, XDG_STATE_HOME=d, XDG_DATA_HOME=d, TERM="xterm-256color")
pid, fd = pty.fork()
if pid == 0:
    os.chdir(d); os.execve(exe, [exe, "-t", "m.sv"], env)
import fcntl, termios, struct
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
def pump(sec):
    end = time.time() + sec; out = b""
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out
pump(1.0)
t0 = time.time()
os.write(fd, b":Format\r"); pump(0.3)
if mode == "ctrlc":
    os.write(fd, b"\x03"); pump(0.1)
    os.write(fd, b"Oafter\x1b:wq\r")
else:
    os.write(fd, b"Oafter\x1b:wq\r")
end = time.time() + 8
while time.time() < end:
    pump(0.05)
    p, st = os.waitpid(pid, os.WNOHANG)
    if p: break
dt = time.time() - t0
got = open(d + "/m.sv").read()
left = subprocess.run(["pgrep", "-f", "sleep 10"], capture_output=True, text=True).stdout.split()
print(f"{mode}: editor exited {dt*1000:.0f} ms after :Format; file {'has' if got == 'after\n' + src else 'LACKS'} the typed line: {got!r}; sleepers left: {len(left)}")
