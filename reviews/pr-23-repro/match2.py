#!/usr/bin/env python3
"""Review of PR 23: the reviewer's own cases for src/tools/match.c against
snacks (the author's match.py runs them): many OR terms, many AND terms,
Lua pattern characters, long gaps, file:line:col shapes.
  match2.py SNACKS_DIR DRIVER N SEED"""
import os, random, sys
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "../../src/tools/conform/match.py")).read().replace("sys.exit(main(sys.argv))", "")
src = src.replace("os.path.abspath(__file__)", repr(os.path.join(here, "../../src/tools/conform/match.py")))
exec(src)
W = ["a", "b", "ab", "Ab", "abc", "a.c", "x/y", "core", "Core", "%s", "(", ")", "[a-z]", "a+", "*", "?", ".", "-", "%", "_", "a_b",
     "é", "日本", "\x7f", "foo.lua", "a:b", "0", "12", "'", "''", "!", "^", "$", "^$", "'a'", "!'a'", "!^a$", "a$", "|", "||", "a|b"]
def text(r):
    k = r.random()
    if k < 0.3: return "/".join(r.choice(W) for _ in range(r.randint(1, 6)))
    if k < 0.5: return r.choice(W) + "x" * r.randint(0, 80) + r.choice(W) + "y" * r.randint(0, 80) + r.choice(W)
    if k < 0.7: return " ".join(r.choice(W) for _ in range(r.randint(0, 5)))
    return "".join(r.choice(W) for _ in range(r.randint(0, 8)))
def pat(r):
    k = r.random()
    if k < 0.25: return " | ".join(r.choice(W) for _ in range(r.randint(2, 9)))
    if k < 0.45: return " ".join(r.choice(W) for _ in range(r.randint(1, 20)))
    if k < 0.65: return r.choice(W) + r.choice([":", ":1", ":12:3", "::", ":0", ":007:08", ":1:", ":a:1", ":1:a"]) + r.choice(["", " " + r.choice(W)])
    if k < 0.8: return r.choice(["file:", "text:", "ab:", "a:", "_x:", "file::", "file:file:"]) + r.choice(W)
    return "".join(r.choice(W + [" ", " | ", "  "]) for _ in range(r.randint(1, 10)))
def case2(r):
    items = []
    for _ in range(r.randint(1, 10)):
        t = text(r)
        f = t if r.random() < 0.6 else None
        items.append({"t": t if f is not None or r.random() < 0.5 else t + " " + r.choice(W), "f": f})
    return {"p": pat(r), "i": items}
sdir, drv, n, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
r = random.Random(seed)
cases = [c for c in (case2(r) for _ in range(n)) if clean(c) and "\x01" not in c["p"]]
exp = snacks(sdir, cases)
got = ours(drv, cases)
bad = [(c, e, g) for c, e, g in zip(cases, exp, got) if tuple(e[0]) != tuple(g[0]) or e[1] != g[1]]
kinds = {}
for c, e, g in bad:
    nt = len([t for t in c["p"].split(" ") if t and t != "|"])
    k = ">16 terms" if nt > 16 else "other"
    kinds.setdefault(k, []).append((c, e, g))
for k, v in kinds.items():
    print("== %s: %d" % (k, len(v)))
    for c, e, g in v[:6]:
        print("DIFF pattern %r" % c["p"])
        for it, x, y in zip(c["i"], e[1], g[1]):
            if x != y: print("  snacks %6d ours %6d  %r file=%r" % (x, y, it["t"], it["f"]))
        if tuple(e[0]) != tuple(g[0]): print("  pos snacks %s ours %s" % (e[0], g[0]))
print("%d cases, %d differ" % (len(cases), len(bad)))
