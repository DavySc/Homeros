# Which of three files does each editor keep in v:oldfiles: /private/..., /TMP/... (Linux, so a
# real other directory), /var/tmp/...?  homeros: one session per file, then read its oldfiles.
import os, sys, glob, time
sys.path.insert(0, "tests")   # run from the repo root
from ptyrun import spawn, drain
binary = sys.argv[1]; st = sys.argv[2]
os.environ["XDG_STATE_HOME"] = st; os.environ["XDG_DATA_HOME"] = st + "/d"
for f in ["/private/p73/a.txt", "/TMP/p73/b.txt", "/var/tmp/p73/c.txt"]:
    pid, fd = spawn([binary, "-t", f]); drain(fd, 0.5)
    os.write(fd, b":q\r"); drain(fd, 1.0); os.waitpid(pid, 0)
for p in glob.glob(st + "/**/oldfiles", recursive=True): print(open(p).read(), end="")
