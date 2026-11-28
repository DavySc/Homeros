# runs the plant lines of FILE (target "test TESTS=<one>", no prep) against that test alone, built with sanitizers through mk.sh.  usage: prun.py TREE FILE [grep]
import subprocess, os, sys
tree, pf = sys.argv[1], os.path.abspath(sys.argv[2]); want = sys.argv[3] if len(sys.argv) > 3 else ""
env = dict(os.environ, SQL_SA_PASSWORD="Homeros_Test1", SQL_PG_PASSWORD="Homeros_Test1", XDG_STATE_HOME="/tmp/rv/state")
os.chdir(tree)
n = c = 0
for line in open(pf):
    line = line.rstrip("\n")
    if not line or line.startswith("#") or want not in line: continue
    name, target, prep, file, old, new = line.split("~", 5)
    if not target.startswith("test TESTS=") or prep: continue
    t = target[11:]
    old, new = old.replace("¶", "\n"), new.replace("¶", "\n")
    src = open(file).read()
    if src.count(old) != 1: print("BROKEN PLANT ", name, src.count(old), flush=True); continue
    n += 1
    open(file, "w").write(src.replace(old, new))
    try:
        if os.path.exists("/tmp/rv/t_pl"): os.remove("/tmp/rv/t_pl")
        b = subprocess.run(["/tmp/rv/mk.sh", ".", "tests/test_%s.c" % t, "/tmp/rv/t_pl"], capture_output=True, text=True)
        if b.returncode or not os.path.exists("/tmp/rv/t_pl"): print("BUILD FAIL   ", name, b.stderr[-300:], flush=True); continue
        try:
            r = subprocess.run(["nsenter", "-t", open("/tmp/rv/ns.pid").read().strip(), "-n", "/tmp/rv/t_pl"], capture_output=True, text=True, env=env, timeout=300)
            ok = r.returncode != 0
        except subprocess.TimeoutExpired: ok = True; name += " (hang)"
        c += ok
        print("caught       " if ok else "NOT CAUGHT   ", name, flush=True)
    finally:
        open(file, "w").write(src); os.utime(file)
print("%d of %d caught" % (c, n))
