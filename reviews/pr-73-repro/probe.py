# Does any command leave the start screen over another buffer?  homeros -t with no file in an
# 80x30 pty, keys, then the screen (pyte): is "Find File" still drawn, and the last row.
import os, sys, tempfile, fcntl, termios, struct
sys.path.insert(0, "tests")   # run from the repo root
from ptyrun import spawn, drain
import pyte
binary = sys.argv[1]
d = tempfile.mkdtemp(dir="/var/tmp"); os.chdir(d)
os.environ["XDG_STATE_HOME"] = d + "/s"; os.environ["XDG_DATA_HOME"] = d + "/d"
os.environ["LINES"], os.environ["COLUMNS"] = "40", "100"
cases = [b"", b"\x17v", b"\x17s", b"\x17v\x17c", b"\x1e", b"r\x1b\x1b"]
for keys in cases:
    pid, fd = spawn([binary, "-t"])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0)); os.kill(pid, 28)
    sc = pyte.Screen(100, 40); st = pyte.ByteStream(sc)
    st.feed(drain(fd, 0.8))
    os.write(fd, keys); st.feed(drain(fd, 1.0))
    shown = any("Find File" in l for l in sc.display)
    print("%-26r start screen %-5s | last row: %s" % (keys, "SHOWN" if shown else "gone", sc.display[-1].strip()[:60]))
    os.write(fd, b"\x1b\x1b:qa!\r"); drain(fd, 0.5)
    try: os.kill(pid, 9); os.waitpid(pid, 0)
    except OSError: pass
