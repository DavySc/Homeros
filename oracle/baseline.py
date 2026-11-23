"""baseline.py ZIG FIRST LAST BACKEND DEBUGGER: runs gdb, lldb or a zdb on the oracle's programs.
Two scores per value: exact (the text std.fmt prints) and numbers (the sequence of numbers,
true, false and null in the text, which ignores how each debugger spells structure)."""
import os, re, subprocess, sys, tempfile
zig, a, b, backend, dbg = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
here = os.path.dirname(os.path.abspath(__file__))
work = tempfile.mkdtemp(prefix="zdb-base-")
import atexit, shutil; atexit.register(shutil.rmtree, work, True)   # each binary is 10 MB
NUM = re.compile(r"(?<![\w.@])-?\d+(?:\.\d+)?(?:e-?\d+)?(?![\w@])|\btrue\b|\bfalse\b|\bnull\b")

def leaves(s):
    s = re.sub(r"0x[0-9a-f]+|\w+@[0-9a-f]+", "", s)      # addresses
    return NUM.findall(s)

def run_gdb(exe, src, probes):
    cmds = ["set pagination off", "set print pretty off", "set confirm off"]
    cmds += [f"break {src}:{ln}" for ln, _ in probes] + ["run"]
    for _, names in probes:
        for n in names:
            cmds += ["echo @@V\\n", f"print {n}"]
        cmds += ["continue"]
    open(f"{exe}.gdb", "w").write("\n".join(cmds) + "\n")
    out = subprocess.run(["gdb", "-nx", "-batch", "-x", f"{exe}.gdb", exe], capture_output=True, text=True, timeout=120, cwd=work).stdout
    vals = []
    for chunk in out.split("@@V\n")[1:]:
        line = chunk.split("\n")[0]
        vals.append(re.sub(r"^\$\d+ = ", "", line))
    return vals

def run_lldb(exe, src, probes):
    cmds = [f"breakpoint set -f {src} -l {ln}" for ln, _ in probes] + ["run"]
    for _, names in probes:
        cmds += [f"frame variable {n}" for n in names] + ["continue"]
    open(f"{exe}.lldb", "w").write("\n".join(cmds) + "\n")
    out = subprocess.run(["lldb", "-b", "-s", f"{exe}.lldb", exe], capture_output=True, text=True, timeout=120, cwd=work)
    vals = []
    for chunk in (out.stdout + out.stderr).split("(lldb) frame variable ")[1:]:
        body = chunk.split("\n", 1)[1] if "\n" in chunk else ""
        line = body.split("\n(lldb)")[0].split("\n")[0]
        vals.append(re.sub(r"^\([^)]*\) \w+ = ", "", line))
    return vals

def run_zdb(exe, src, probes):
    out = subprocess.run([dbg, "probe", exe, src, f"{exe}.probes"], capture_output=True, text=True, timeout=120, cwd=work,
                         env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    return [l for l in out.stdout.splitlines() if not l.startswith("# ")], out.stderr.splitlines()

exact = nums = total = progs = 0
for seed in range(a, b + 1):
    src = f"p{seed}.zig"; exe = f"{work}/p{seed}"
    with open(f"{work}/{src}", "w") as o, open(f"{exe}.probes", "w") as e:
        subprocess.run([sys.executable, "-I", f"{here}/gen.py", str(seed)], stdout=o, stderr=e, check=True)
    flags = ["-fllvm"] if backend == "llvm" else []
    if subprocess.run([zig, "build-exe", src, f"-femit-bin={exe}", *flags], capture_output=True, cwd=work).returncode:
        continue
    progs += 1
    probes = [(int(l.split()[1]), l.split()[2:]) for l in open(f"{exe}.probes")]
    want = subprocess.run([exe], capture_output=True, text=True, cwd=work).stderr.splitlines()
    if dbg == "gdb": got = run_gdb(exe, src, probes)
    elif dbg == "lldb": got = run_lldb(exe, src, probes)
    else: got, want = run_zdb(exe, src, probes)   # zdb runs it itself: same addresses for pointers
    for i, w in enumerate(want):
        g = got[i] if i < len(got) else ""
        total += 1
        exact += g == w
        nums += leaves(g) == leaves(w)
        if seed == a and i < 6: print(f"  want {w!r}\n  got  {g!r}")
print(f"BASELINE debugger={os.path.basename(dbg)} backend={backend} programs={progs} values={total} exact={exact} numbers={nums}")
