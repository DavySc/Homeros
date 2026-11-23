"""score.py ZIG ZDB FIRST LAST [BACKEND]: generates programs FIRST..LAST, builds each, runs ZDB on
it and compares every printed value with the program's own std.fmt output. Prints mismatches and
a summary line. BACKEND: self (default) or llvm."""
import os, subprocess, sys, tempfile
zig, zdb, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
backend = sys.argv[5] if len(sys.argv) > 5 else "self"
here = os.path.dirname(os.path.abspath(__file__))
work = tempfile.mkdtemp(prefix="zdb-oracle-")
import atexit, shutil; atexit.register(shutil.rmtree, work, True)   # each binary is 10 MB
ok = bad = skipped = progs = absent = 0
kinds = {}
for seed in range(a, b + 1):
    src, exe, pf = f"{work}/p{seed}.zig", f"{work}/p{seed}", f"{work}/p{seed}.probes"
    with open(src, "w") as o, open(pf, "w") as e:
        subprocess.run([sys.executable, "-I", f"{here}/gen.py", str(seed)], stdout=o, stderr=e, check=True)
    flags = ["-fllvm"] if backend == "llvm" else []
    c = subprocess.run([zig, "build-exe", src, f"-femit-bin={exe}", *flags], capture_output=True, text=True, cwd=work)
    if c.returncode:
        skipped += 1
        continue
    progs += 1
    run = subprocess.run([zdb, "probe", exe, os.path.basename(src), pf], capture_output=True, text=True, cwd=work, timeout=60,
                         env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    want = run.stderr.splitlines()
    got = [l for l in run.stdout.splitlines() if not l.startswith("# ")]
    names = [n for l in open(pf) for n in l.split()[2:]]
    for i, w in enumerate(want):
        g = got[i] if i < len(got) else "<missing>"
        if g == w:
            ok += 1
        elif g.startswith("<no variable"):
            absent += 1
        else:
            bad += 1
            if bad <= 40:
                print(f"seed {seed} {names[i] if i < len(names) else '?'}: want {w!r}\n{' ' * 10}got  {g!r}")
    if len(got) > len(want):
        print(f"seed {seed}: {len(got) - len(want)} extra lines")
print(f"SUMMARY backend={backend} programs={progs} not_compiled={skipped} values={ok + bad + absent} match={ok} mismatch={bad} absent_from_debug_info={absent}")
