"""SPC g then NUL (Ctrl-Space) in the editor in a pty; prints the sanitizer's lines.  usage: ctrl-space.py BINARY FILE"""
import os, pty, select, sys, time, re
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'; os.environ.pop('WAYLAND_DISPLAY', None)
    os.execvp(sys.argv[1], sys.argv[1:])
out = b''
def drain(t):
    global out
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.02)[0]:
            try: out += os.read(fd, 65536)
            except OSError: return
select.select([fd], [], [], 5); drain(0.6)
os.write(fd, b' g\x00'); drain(1.0)
for l in out.decode('utf8', 'replace').splitlines():
    if re.search(r'ERROR|SUMMARY|#[01] ', l): print(l.strip()[:160])
try: os.kill(pid, 9)
except OSError: pass
