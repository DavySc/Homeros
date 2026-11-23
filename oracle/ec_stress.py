"""ec_stress.py ZIG ZDB FIRST LAST: random edit-and-continue trials. Each trial stops a random
program at iteration k of its loop, edits it (the callee, the stopped function, a new global or
all three), saves, and runs on. Truth: iterations before k follow the old program, the rest the
new one; a refused patch leaves the old program running."""
import atexit, os, random, shutil, subprocess, sys, tempfile, time
zig, zdb, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
M = (1 << 64) - 1
TPL = '''const std = @import("std");
var total: u64 = 0;
fn f(i: u64) u64 {{
    return {fexpr};
}}
pub fn main() void {{
    var i: u64 = 0;
    while (i < 20) : (i += 1) {{
        total +%= f(i){extra};
        std.debug.print("{{d}}\\n", .{{total}});
    }}
}}
{glob}'''

def expr(R, depth=0):
    if depth > 2 or R.random() < 0.3:
        return R.choice(["i", str(R.randint(0, 99)), "g" ]) if R.random() < 0.9 else "i"
    return f"({expr(R, depth + 1)} {R.choice(['+%', '*%', '^', '-%', '|', '&'])} {expr(R, depth + 1)})"

def ev(e, i, g):
    if e.startswith("blk:"):
        inner = e.split("var w: u64 = ")[1].split("; w +%= i;")[0]
        return (ev(inner, i, g) + i) & M
    return eval(e.replace("+%", "+").replace("*%", "*").replace("-%", "-").replace("i", str(i)).replace("g", str(g))) & M

def run_prog(fexpr, extra, g):
    def step(i):
        x = (int(extra.split("var v: u64 = ")[1].split(";")[0]) if "var v: u64" in extra else int(extra.split("@splat(")[1].split(")")[0])
             if "@splat" in extra else int(extra.split()[-1]) if extra else 0)
        return (ev(fexpr, i, g) + x) & M
    return step

stats = dict(trials=0, patched=0, refused=0, compile_err=0, right=0, wrong=0, ms=[])
reasons = {}
for seed in range(a, b + 1):
    R = random.Random(seed)
    work = tempfile.mkdtemp(prefix="zdb-ec-")
    atexit.register(shutil.rmtree, work, True)                 # each binary is 10 MB
    g0 = R.randint(0, 50)
    old = dict(fexpr=expr(R), extra=R.choice(["", f" +% {R.randint(1, 9)}"]))
    new = dict(old)
    kind = R.choice(["callee", "caller", "both", "global", "newlocal", "calleelocal", "biglocal"])
    if kind in ("callee", "both"): new["fexpr"] = expr(R)
    if kind in ("caller", "both"): new["extra"] = R.choice(["", f" +% {R.randint(10, 99)}"])
    g1 = g0
    if kind == "global": g1 = R.randint(51, 99); new["fexpr"] = f"({new['fexpr']}) +% g"
    if kind == "newlocal": c = R.randint(1, 9); new["extra"] = f" +% blk: {{ var v: u64 = {c}; v +%= 0; break :blk v; }}"; new["c"] = c
    if kind == "biglocal":     # a 256-byte local: the frame must grow, the patch must be refused
        c = R.randint(1, 9); new["extra"] = f" +% blk: {{ var v: [32]u64 = @splat({c}); v[1] +%= 0; break :blk v[1]; }}"
    if kind == "calleelocal": new["fexpr"] = f"blk: {{ var w: u64 = {expr(R)}; w +%= i; break :blk w; }}"
    src = f"{work}/p.zig"
    fmt = lambda d, glob: TPL.format(fexpr=d["fexpr"], extra=d["extra"], glob=glob)
    open(src, "w").write(fmt(old, f"var g: u64 = {g0};\n"))
    if subprocess.run([zig, "ast-check", src], capture_output=True).returncode: continue
    k = R.randint(1, 10)
    p = subprocess.Popen([zdb, "session", zig, "p.zig"], cwd=work, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    def read():
        out = []
        while True:
            l = p.stdout.readline()
            if not l or l.strip() == "(zdb)": return out
            out.append(l.rstrip())
    def cmd(c):
        p.stdin.write(c + "\n"); p.stdin.flush(); return read()
    if any("error" in l for l in read()):       # the old program does not build (a constant overflows): no trial
        p.kill(); p.wait(); continue
    cmd("b 9")
    for _ in range(k + 1): cmd("c")         # stopped at iteration k
    glob = f"var g: u64 = {g1};\n" if kind == "global" else f"var g: u64 = {g0};\n"
    open(src, "w").write(fmt(new, glob))
    t = time.time(); r = cmd("w"); stats["ms"].append((time.time() - t) * 1e3)
    cmd("d"); cmd("c")
    try: p.stdin.write("q\n"); p.stdin.flush()
    except BrokenPipeError: pass
    err = p.communicate(timeout=60)[1]
    got = [int(x) for x in err.split() if x.isdigit()]
    stats["trials"] += 1
    text = " ".join(r)
    if "compile error" in text: stats["compile_err"] += 1; patched = False
    elif "refused" in text: stats["refused"] += 1; patched = False; why = text.split("refused: ")[1].split(":")[-1].strip(); reasons[why] = reasons.get(why, 0) + 1
    else: stats["patched"] += 1; patched = True
    # truth: old code for i < k; new code from k on if patched (the global keeps its runtime value: g0)
    # a global the old program never used was never in it (Zig analyses lazily): the new value holds
    gl = g0 if "g" in old["fexpr"] else g1
    so, sn = run_prog(old["fexpr"], old["extra"], g0), run_prog(new["fexpr"], new["extra"], gl)
    tot, want = 0, []
    for i in range(20):
        tot = (tot + (sn(i) if patched and i >= k else so(i))) & M
        want.append(tot)
    key = f"{kind}:{'remap' if 'goes on' in text else 'patched' if patched else 'refused' if 'refused' in text else 'error'}"
    reasons[key] = reasons.get(key, 0) + 1
    if got == want: stats["right"] += 1
    else:
        stats["wrong"] += 1
        if stats["wrong"] <= 5: print(f"seed {seed} {kind} k={k} {text}\n  old {old} new {new}\n  want {want[k:k+3]} got {got[k:k+3]}")
ms = sorted(stats.pop("ms"))
print("EC", stats, "reasons", reasons, f"w median {ms[len(ms)//2]:.0f} ms range {ms[0]:.0f}-{ms[-1]:.0f}")
