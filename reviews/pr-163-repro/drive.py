# usage: pty.py HOMEROS step...   step = name=keys (python escapes) | name=@secs (just wait)
import os, pty, sys, time, select, pyte, tempfile
d = tempfile.mkdtemp(prefix="sqlpty")
os.makedirs(d + "/homeros"); os.makedirs(d + "/ms")
open(d + "/homeros/sql", "w").write("ms mssql 127.0.0.1 sa master trust pass=Homeros_Test1\ndir %s/ms ms\n" % d)
os.chmod(d + "/homeros/sql", 0o600)
open(d + "/ms/q.sql", "w").write(os.environ.get("Q") or "waitfor delay '00:00:08'\nselect 'late' as x\n")
open(d + "/ms/n.txt", "w").write("one two three\nfour five six\nseven eight nine\nten\n")
env = dict(os.environ, XDG_CONFIG_HOME=d, XDG_STATE_HOME=d, TERM="xterm-256color", HOME=d)
env.pop("WAYLAND_DISPLAY", None)
pid, fd = pty.fork()
import fcntl, termios, struct
if pid == 0:
    os.chdir(d + "/ms")
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 90, 0, 0))
    os.execve(sys.argv[1], [sys.argv[1], "q.sql"], env)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 90, 0, 0))
scr = pyte.Screen(90, 24); st = pyte.ByteStream(scr)
def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: st.feed(os.read(fd, 65536))
            except OSError: return
def shot(name):
    print("=" * 30, name, "cursor", scr.cursor.y, scr.cursor.x)
    for l in scr.display:
        if l.strip() and l.strip() != "~": print(l.rstrip())
pump(1.5)
for step in sys.argv[2:]:
    name, keys = step.split("=", 1)
    if keys.startswith("@"): pump(float(keys[1:]))
    else:
        os.write(fd, keys.encode().decode("unicode_escape").encode("latin1")); pump(1.0)
    if not name.startswith("-"): shot(name)
