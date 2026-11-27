import os, pty, sys, time, select, pyte, tempfile, subprocess
d = tempfile.mkdtemp(prefix="sqlpty")
os.makedirs(d + "/homeros"); os.makedirs(d + "/ms")
open(d + "/homeros/sql", "w").write("ms mssql 127.0.0.1 sa master trust pass=Homeros_Test1\ndir %s/ms ms\n" % d)
os.chmod(d + "/homeros/sql", 0o600)
open(d + "/ms/q.sql", "w").write("select top 5 name, object_id, create_date\nfrom sys.all_objects\norder by name\ngo\nprint 'done'\nselect cast(1.23 as money) as m, cast(1e20 as float) as f, N'café' as s, null as n\n")
env = dict(os.environ, XDG_CONFIG_HOME=d, XDG_STATE_HOME=d, TERM="xterm-256color", HOME=d)
pid, fd = pty.fork()
if pid == 0:
    os.chdir(d + "/ms")
    import fcntl, termios, struct
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    os.execve(sys.argv[1], [sys.argv[1], "q.sql"], env)
import fcntl, termios, struct
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
scr = pyte.Screen(100, 30); st = pyte.ByteStream(scr)
def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: st.feed(os.read(fd, 65536))
            except OSError: return
def shot(name):
    print("=" * 30, name)
    for l in scr.display: print(l.rstrip())
pump(1.5)
for step in sys.argv[2:]:
    name, keys = step.split("=", 1)
    os.write(fd, keys.encode().decode("unicode_escape").encode("latin1"))
    pump(1.5)
    shot(name)
