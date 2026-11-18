#!/usr/bin/env python3
"""Differential test of src/tools/match.c against snacks.picker's matcher.

  match.py SNACKS_DIR DRIVER N SEED            fresh random cases, live nvim
  match.py SNACKS_DIR DRIVER --record N SEED FILE   record them for tests/
  match.py - DRIVER --replay FILE              replay recorded expectations

DRIVER reads the same case format as the recording on stdin and prints
the line and scores; the recording is: P pattern / I file|- text / F line
col / S score lines, tab-separated, no tabs or newlines inside."""
import json, os, random, subprocess, sys, tempfile

PARTS = ["src", "lua", "snacks", "picker", "core", "fooBar", "baz_qux", "init",
         "Makefile", "README", "a1b2", "x", "test-case", "UPPER", "camelCase",
         "my.file", "v2", "ä", "dot.d", "..", "HTTPServer", "data.json"]
EXT = [".c", ".h", ".lua", ".md", "", ".tar.gz", ".PY"]
CH = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJ0123456789/_-.: ,;|\\'!^$ä"

def item(r):
    n = r.randint(1, 4)
    p = "/".join(r.choice(PARTS) for _ in range(n)) + r.choice(EXT)
    if r.random() < 0.15:
        p = "".join(r.choice(CH) for _ in range(r.randint(1, 30)))
    if r.random() < 0.1:
        p = r.choice(["", "/", " " + p, p + ":12"])
    f = p if r.random() < 0.7 else None
    t = p if f is not None or r.random() < 0.5 else p + " " + r.choice(PARTS)
    return {"t": t, "f": f}

def term(r, items):
    src = r.choice(items)["t"] or "a"
    k = r.random()
    if k < 0.5:   # a subsequence of an item, so it often matches
        i = r.randrange(len(src)); j = min(len(src), i + r.randint(1, 8))
        s = "".join(c for c in src[i:j] if r.random() < 0.8) or src[i:j]
    elif k < 0.8:
        s = src[r.randrange(len(src)):][: r.randint(1, 6)]
    else:
        s = "".join(r.choice(CH) for _ in range(r.randint(1, 6)))
    if r.random() < 0.3: s = s.swapcase() if r.random() < 0.5 else s.upper()
    pre = r.choice([""] * 12 + ["!", "'", "^", "!'", "!^", "file:", "text:", "name:"])
    post = r.choice([""] * 12 + ["$", "'", ":3", ":3:4", ":", "::7"])
    return pre + s + post

def case(r):
    items = [item(r) for _ in range(r.randint(1, 12))]
    toks = [term(r, items) for _ in range(r.choice([0, 1, 1, 1, 1, 2, 2, 3, 4]))]
    sep = [r.choice([" ", " ", "  ", " | "]) for _ in toks]
    p = "".join(t + s for t, s in zip(toks, sep))
    if r.random() < 0.2: p = " " + p + r.choice(["", "|", " "])
    return {"p": p, "i": items}

def clean(c):   # no tabs or newlines in the recording
    return "\t" not in c["p"] and "\n" not in c["p"] and all("\t" not in i["t"] and "\n" not in i["t"] for i in c["i"])

def record(cases, exp):
    out = []
    for c, (fl, sc) in zip(cases, exp):
        out.append("P\t" + c["p"])
        for i in c["i"]:
            out.append("I\t" + (i["f"] if i["f"] is not None else "\x01") + "\t" + i["t"])
        out.append("F\t%d\t%d" % fl)
        out += ["S\t%d" % s for s in sc]
    return "\n".join(out) + "\n"

def parse(text):
    cases, exp, c = [], [], None
    for ln in text.split("\n"):
        if not ln: continue
        k, rest = ln[0], ln[2:]
        if k == "P": c = {"p": rest, "i": []}; cases.append(c); exp.append([None, []])
        elif k == "I":
            f, t = rest.split("\t", 1)
            c["i"].append({"t": t, "f": None if f == "\x01" else f})
        elif k == "F": exp[-1][0] = tuple(int(x) for x in rest.split("\t"))
        elif k == "S": exp[-1][1].append(int(rest))
    return cases, exp

def snacks(sdir, cases):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "in"), os.path.join(d, "out")
        with open(i, "w") as f:
            for c in cases: f.write(json.dumps(c) + "\n")
        lua = os.path.join(os.path.dirname(os.path.abspath(__file__)), "match.lua")
        subprocess.run(["nvim", "--clean", "--headless", "-l", lua, sdir, i, o], check=True)
        res, cur = [], None
        for ln in open(o).read().split("\n"):
            if not ln: continue
            if ln.startswith("F "):
                cur = [tuple(int(x) for x in ln[2:].split()), []]; res.append(cur)
            else: cur[1].append(int(float(ln)))
        return res

def ours(drv, cases):
    p = subprocess.run([drv], input=record(cases, [((0, 0), [])] * len(cases)).encode(), capture_output=True, check=True)
    return parse(p.stdout.decode())[1]

def main(a):
    sdir, drv = a[1], a[2]
    if a[3] == "--replay":
        cases, exp = parse(open(a[4]).read())
    else:
        n, seed = int(a[3 + (a[3] == "--record")]), int(a[4 + (a[3] == "--record")])
        r = random.Random(seed)
        cases = [c for c in (case(r) for _ in range(n)) if clean(c)]
        exp = snacks(sdir, cases)
        if a[3] == "--record":
            open(a[6], "w").write(record(cases, exp)); print("recorded", len(cases)); return 0
    got = ours(drv, cases)
    bad = [(c, e, g) for c, e, g in zip(cases, exp, got) if tuple(e[0]) != tuple(g[0]) or e[1] != g[1]]
    for c, e, g in bad[:5]:
        print("DIFF pattern %r" % c["p"])
        for it, x, y in zip(c["i"], e[1], g[1]):
            print("  %s %6d %6d  %r file=%r" % ("*" if x != y else " ", x, y, it["t"], it["f"]))
        if e[0] != g[0]: print("  pos snacks %s ours %s" % (e[0], g[0]))
    print("%d cases, %d differ" % (len(cases), len(bad)))
    return 1 if bad else 0

sys.exit(main(sys.argv))
