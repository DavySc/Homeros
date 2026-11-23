"""panic_stress.py ZIG ZDB FIRST LAST: random fix-after-panic trials.
Each seed makes a program whose function hits a Zig safety check on some iteration. zdb runs it,
stops at the panic, the check's line is edited into a form that gives the same value whenever
the check passes (x * k -> x *% k, a[x] -> a[x % a.len], ...), then w and c. The truth is the
fixed program run from the start: its output must equal what the debugged process printed."""
import os, random, subprocess, sys, tempfile
zig, zdb, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
work = tempfile.mkdtemp(prefix="zdb-panic-")
import atexit, shutil; atexit.register(shutil.rmtree, work, True)   # each binary is 10 MB
ENV = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}

CHECKS = [  # (setup, expression, fixed expression); x: u8 from the iteration, y: u32, opt: ?u8
    ("", "x * {k}", "x *% {k}"),
    ("", "x + {k}", "x +% {k}"),
    ("", "x - {k}", "x -% {k}"),
    ("const arr = [_]u8{{ 7, 1, 9, 4, 2 }};\n", "arr[x]", "arr[x % arr.len]"),
    ("", "@as(u8, @intCast(y))", "@as(u8, @truncate(y))"),
    ("const opt: ?u8 = if (x == {k7}) null else x;\n", "opt.?", "(opt orelse 0)"),
    ("", "x / (x -% {k7})", "x / ((x -% {k7}) | 1)"),
]
TYPES = [("u8", "@truncate(i *% {m})"), ("u16", "@truncate(i *% {m} + 3)"), ("u64", "@as(u64, i) * {m}"),
         ("i32", "@as(i32, @intCast(i)) - {m}"), ("bool", "i % {m} == 0"), ("[3]u16", ".{{ @truncate(i), {m}, @truncate(i *% {m}) }}"),
         ("S", ".{{ .a = @truncate(i), .b = i % {m} == 1 }}")]

PARAMS = [("a: u8", "@truncate(i)", "a"), ("b: u64", "@as(u64, i) << 33", "@truncate(b >> 33)"), ("c: bool", "i % 2 == 0", "@intFromBool(c)"),
          ("s: S", ".{ .a = @truncate(i), .b = true }", "s.a"), ("t: []const u8", "\"hello\"[0 .. i % 5]", "@intCast(t.len)"),
          ("q: *const u32", "&gl", "q.*"), ("fl: f64", "@floatFromInt(i)", "@intFromFloat(fl)"), ("e: E", "@enumFromInt(i % 3)", "@intFromEnum(e)")]

def program(rng):
    """the program, the fixed program, the check's line"""
    setup, expr, fix = rng.choice(CHECKS)
    k, k7 = rng.randint(2, 9), 7 * rng.randint(1, 5)
    setup, expr, fix = (s.format(k=k, k7=k7) for s in (setup, expr, fix))
    params = rng.sample(PARAMS, rng.randint(0, 3))
    locs = [(f"v{j}",) + rng.choice(TYPES) for j in range(rng.randint(0, 4))]
    lines = ["const std = @import(\"std\");", "const S = struct { a: u8, b: bool };", "const E = enum { x, y, z };", "var gl: u32 = 5;"]
    lines.append("fn f(i: u32" + "".join(", " + p[0] for p in params) + ") u32 {")
    for name, ty, init in locs:
        lines.append(f"    var {name}: {ty} = {init.format(m=rng.randint(2, 7))};")
        lines.append(f"    _ = &{name};")
    lines.append("    var acc: u32 = 0;")
    for p in params: lines.append(f"    acc +%= {p[2]};")
    ind, loop = "    ", rng.choice(["", "for", "while"])
    if loop == "for": lines.append("    for (0..3) |j| {"); ind = "        "
    if loop == "while": lines.append("    var j: u32 = 0;"); lines.append("    while (j < 3) : (j += 1) {"); ind = "        "
    jv = "@as(u32, @intCast(j))" if loop == "for" else ("j" if loop == "while" else "0")
    lines.append(f"{ind}const x: u8 = @truncate(i * 7 + {jv});")
    lines.append(f"{ind}const y: u32 = i * 40 + {jv};")
    lines.append(f"{ind}_ = &y;")
    lines.append(f"{ind}_ = &x;")
    for l in setup.splitlines(): lines.append(ind + l)
    check_line = len(lines) + 1
    lines.append(f"{ind}const r: u8 = {expr};")
    lines.append(f"{ind}acc +%= r;")
    if loop: lines.append("    }")
    for name, _, _ in locs: lines.append(f"    std.debug.print(\"{name}={{any}}\\n\", .{{{name}}});")
    lines.append("    return acc;")
    lines.append("}")
    call = "f(i" + "".join(", " + p[1] for p in params) + ")"
    middle = rng.random() < 0.4               # f called from another function of the file
    if middle:
        lines += ["fn h(i: u32) u32 {", "    var w: u32 = i *% 3;", "    _ = &w;", f"    return {call} +% w;", "}"]
        call = "h(i)"
    lines += ["pub fn main() void {", "    var i: u32 = 0;", "    while (i < 40) : (i += 1) {", f"        const v = {call};",
              "        std.debug.print(\"{d} {d}\\n\", .{ i, v });", "    }", "}"]
    src = "\n".join(lines) + "\n"
    old_line = lines[check_line - 1]
    return src, src.replace(old_line, old_line.replace(expr, fix)), check_line

def zdb_run(src, fixed):
    with open(f"{work}/t.zig", "w") as o: o.write(src)
    for f in os.listdir(work):
        if f.startswith("zdb-"): os.remove(f"{work}/{f}")
    p = subprocess.Popen([zdb, "session", zig, "t.zig"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=work, env=ENV)
    def cmd(c):
        if c: p.stdin.write(c + "\n"); p.stdin.flush()
        out = ""
        while True:
            l = p.stdout.readline()
            if not l or "(zdb)" in l: return out
            out += l
    log = cmd("")
    if "built" not in log: p.kill(); return "nobuild", log, ""
    stop = cmd("c"); log += stop
    if "panic:" not in stop: p.kill(); p.wait(); return "nopanic", log, ""
    if " outside " in stop: p.kill(); p.wait(); return "outside", log, ""
    with open(f"{work}/t.zig", "w") as o: o.write(fixed)
    w = cmd("w"); log += w
    if "refused" in w: p.kill(); p.wait(); return "refused", log, ""
    log += cmd("c")
    try: p.stdin.write("q\n"); p.stdin.close()
    except BrokenPipeError: pass
    err = p.stderr.read(); p.wait()
    return "ran", log, err

count = {}
bad = []
for seed in range(a, b + 1):
    rng = random.Random(seed)
    src, fixed, line = program(rng)
    with open(f"{work}/fix.zig", "w") as o: o.write(fixed)
    fb = subprocess.run([zig, "build-exe", "fix.zig"], cwd=work, capture_output=True, text=True)
    if fb.returncode: print(f"seed {seed} fix does not build: {fb.stderr.splitlines()[0] if fb.stderr else ''}"); count["fix-nobuild"] = count.get("fix-nobuild", 0) + 1; continue
    want = subprocess.run([f"{work}/fix"], cwd=work, capture_output=True, text=True).stderr
    kind, log, got = zdb_run(src, fixed)
    if kind == "ran": kind = "right" if got == want else "WRONG"
    count[kind] = count.get(kind, 0) + 1
    if kind in ("WRONG", "refused", "nopanic", "outside") and len(bad) < 400:
        bad.append(seed)
        print(f"seed {seed} {kind}: {log.strip().splitlines()[-1] if log.strip() else ''}")
        if kind == "WRONG":
            gl, wl = got.splitlines(), want.splitlines()
            for i in range(max(len(gl), len(wl))):
                if (gl[i:i+1] or [""]) != (wl[i:i+1] or [""]): print(f"  first difference at output line {i}: got {gl[i:i+1]} want {wl[i:i+1]}"); break
print("PANIC", " ".join(f"{k}={v}" for k, v in sorted(count.items())))
