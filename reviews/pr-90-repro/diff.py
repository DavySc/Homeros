#!/usr/bin/env python3
"""diff.py PROBE ORACLE.lua N SEED: random searches and :s with pieces the PR's
test does not draw (look-arounds, \\{-}, \\&, \\%^, \\_[..], back references,
\\v), ours (probe) against nvim through the PR's ex_oracle.lua.  Prints each
case that differs in cursor, error number or text."""
import os, random, subprocess, sys, tempfile
probe, oracle, n, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
random.seed(seed)
TEXT = "foo bar foo\nabc Foo x1 22\n  foo.bar(a) 12\nbaz? aXa bXb\n\nx/y?z foo\nfoofoo zz\nend of it\n"
ATOM = ["foo", "o", "a", "b", "x", ".", "\\d", "\\s", "\\w", "[ab]", "[^o ]", "\\_s", "\\n", "\\_.", "\\_[^z]", "\\(o\\)", "\\(.\\)", "\\(a\\|b\\)",
        "\\(fo\\|foo\\)", "\\1", "\\<", "\\>", "^", "$", "\\%^", "\\%$", "\\zs", "\\ze", "\\&", "\\|", "\\%(o\\|a\\)", "z", " ", "\\a", "\\u", "\\S", "X", "\\_^", "\\_$"]
MULTI = ["", "", "", "*", "\\+", "\\=", "\\?", "\\{-}", "\\{2}", "\\{-1,}", "\\{,2}", "\\@=", "\\@!", "\\@<=", "\\@<!", "\\@>"]
VATOM = ["foo", "o", "a", ".", "\\d", "(o)", "(a|b)", "<", ">", "\\n", "\\_s", "%(fo|foo)", "\\1", "[ab]", " ", "\\zs", "x"]
VMULTI = ["", "", "*", "+", "=", "?", "{-}", "{2}", "@=", "@!", "@<=", "@<!", "@>"]
REP = ["", "X", "&", "\\1", "[&]", "\\r", "\\U&", "\\u\\1", "~", "-"]
PRE = ["", "", "j", "G", "jjw", "3j$", "2", "d", "c", "y"]
AFTER = ["", "", "n", "N", "nn", "2n", "u", ".", "&", "g&", "*", "#"]
def pat():
    if random.random() < .2:
        return "\\v" + "".join(random.choice(VATOM) + random.choice(VMULTI) for _ in range(random.randint(1, 3)))
    return "".join(random.choice(ATOM) + random.choice(MULTI) for _ in range(random.randint(1, 3)))
def case():
    p = pat()
    if random.random() < .5:
        d = random.choice("/?")
        if d in p: d = "/" if "/" not in p else None
        if d is None or "/" in p and d == "/": return case()
        k = random.choice(PRE) + d + p + random.choice(["", "", d + "e", d + "+1", d + "s-1", d + "e+1"]) + "\r"
        if k[0] == "c": k += "Q\x1b"
    else:
        if "/" in p: return case()
        k = random.choice(["", "j", "G"]) + ":" + random.choice(["", "%", "%", "2,5", ".,$", "3"]) + "s/" + p + "/" + random.choice(REP) + "/" + random.choice(["", "g", "g", "gi", "n", "ge"]) + "\r"
    return k + random.choice(AFTER)
d = tempfile.mkdtemp()
tf, cf, out = d + "/text", d + "/cases", d + "/out"
open(tf, "w").write(TEXT)
cases = [case() for _ in range(n)]
open(cf, "wb").write("".join(c + "\n" for c in cases).encode())
subprocess.run("seq %d | xargs -P 3 -I{} timeout 10 nvim --headless --clean -l %s %s %s %s.{} {} 2>/dev/null" % (n, oracle, tf, cf, out), shell=True)
bad = none = crash = 0
for i, c in enumerate(cases):
    try:
        w = open("%s.%d" % (out, i + 1), "rb").read().decode(errors="replace").split("\n")
        wl, wc, we = w[0].split(); wt = w[1].split("\x01")
    except Exception:
        none += 1; continue
    arg = "".join("\\x%02x" % b for b in c.encode())
    # a fresh state directory: the probe leaves without closing, and its journal would be recovered by the next
    st = tempfile.mkdtemp()
    try: r = subprocess.run([probe, tf, arg], capture_output=True, timeout=20, env=dict(os.environ, XDG_STATE_HOME=st))
    except subprocess.TimeoutExpired:
        crash += 1; print("HANG", repr(c)); continue
    if r.returncode:
        crash += 1; print("CRASH rc=%d" % r.returncode, repr(c), r.stderr.decode(errors="replace")[:300]); continue
    o = r.stdout.decode(errors="replace").split("\n")
    if os.environ.get("DBG"): print(repr(c), o, w)
    h = o[0].split(" msg [")
    gl, gc = h[0].split()[1], h[0].split()[3]
    m = h[1]
    ge = m.split(":")[0] if m.startswith("E") else "-"
    gt = [x[1:-1] for x in o[1:] if x.startswith("|")]
    # ours shows only the last message: compare the error only when Neovim's last error is ours
    if (gl, gc) != (wl, wc) or gt != wt or (ge != "-" and we != ge) or (we != "-" and ge == "-" and (gl, gc, gt) != (wl, wc, wt)):
        bad += 1
        if bad <= 40: print("DIFF", repr(c), "ours", gl, gc, ge, "nvim", wl, wc, we, "(text differs)" if gt != wt else "")
print("%d cases: %d differ, %d crash or hang, %d without a Neovim answer" % (n, bad, crash, none))
