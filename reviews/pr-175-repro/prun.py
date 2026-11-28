"""PR 175 review: runs plant lines through cached sanitizer objects.
  prun.py TREE PLANTFILE [GREP]
TREE is a worktree apart from the one make check runs in (its build/
links to a built tree, build/rvobj copied).  Each "test TESTS=<one>" line
is applied, that one test built (mk.sh) and run, and the file restored.
Tests that need the servers run in the network namespace of a second
pair of containers (/tmp/rv/ns.pid), so make check's servers stay its own.
A plant that breaks the build is reported as BUILD FAIL, never as caught."""
import subprocess, os, sys
tree, pf = sys.argv[1], os.path.abspath(sys.argv[2]); grep = sys.argv[3] if len(sys.argv) > 3 else ''
mk = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mk.sh')
env = dict(os.environ, SQL_SA_PASSWORD="Homeros_Test1", SQL_PG_PASSWORD="Homeros_Test1")
ns = open('/tmp/rv/ns.pid').read().strip()
os.chdir(tree)
n = c = 0
for line in open(pf):
    line = line.rstrip("\n")
    if not line or line.startswith("#") or grep not in line: continue
    name, target, prep, file, old, new = line.split("~", 5)
    if not target.startswith("test TESTS=") or prep: print("SKIPPED      ", name); continue
    t = target[11:]
    old, new = old.replace("¶", "\n"), new.replace("¶", "\n")
    src = open(file).read()
    if src.count(old) != 1: print("BROKEN PLANT ", name, flush=True); continue
    n += 1
    open(file, "w").write(src.replace(old, new))
    try:
        out = "/tmp/rv/t_pl_%d" % os.getpid()
        if os.path.exists(out): os.remove(out)
        b = subprocess.run(["bash", mk, ".", "tests/test_%s.c" % t, out], capture_output=True, text=True)
        if b.returncode or not os.path.exists(out): print("BUILD FAIL   ", name, b.stderr[-300:], flush=True); continue
        cmd = [out] if not t.startswith("sql_e") else ["nsenter", "-t", ns, "-n", out]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=400)
            ok = r.returncode != 0
            why = (r.stderr.strip().splitlines() or [''])[-1][:150]
        except subprocess.TimeoutExpired: ok = True; why = "(hang)"
        c += ok
        print("caught       " if ok else "NOT CAUGHT   ", name, "::", why if ok else "", flush=True)
    finally:
        open(file, "w").write(src); os.utime(file)
print("%d of %d caught" % (c, n))
