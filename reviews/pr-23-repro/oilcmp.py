#!/usr/bin/env python3
"""Review of PR 23: runs each case of an oil oracle output (oil.lua or
oil2.lua, after filter.awk) through the driver on its own and groups the
differences.  oilcmp.py DRIVER FILE [-v]"""
import subprocess, sys, re, collections
drv, f = sys.argv[1], sys.argv[2]
cases, cur = [], None
for ln in open(f, encoding="utf-8", errors="surrogateescape"):
    if ln.startswith("C "): cur = [ln]; cases.append(cur)
    elif ln.strip() == ".": cur = None
    elif cur is not None: cur.append(ln)
groups = collections.defaultdict(list)
for c in cases:
    p = subprocess.run([drv, "-"], input=("".join(c) + ".\n").encode("utf-8", "surrogateescape"), capture_output=True)
    if b"0 differ" in p.stdout: continue
    err = p.stderr.decode("utf-8", "replace")
    if "rendered" in err:
        m = re.search(r"oil rendered\n(.*?)ours\n(.*?)(?:case |tests/|$)", err, re.S)
        a, b = m.group(1).split("\n"), m.group(2).split("\n")
        d = [(x, y) for x, y in zip(a, b) if x != y]
        key = "render: link target ending in /" if d and all(y == x + "/" for x, y in d) and len(a) == len(b) else "render: other"
    else:
        m = re.search(r": oil\n(.*?)ours\n(.*?)(?:tests/|$)", err, re.S)
        o, u = (m.group(1), m.group(2)) if m else ("?", "?")
        ox = sorted(set(re.findall(r"^X \S+ \S+ (.*)$", o, re.M))); ux = sorted(set(re.findall(r"^X \S+ \S+ (.*)$", u, re.M)))
        if p.returncode < 0 or "Sanitizer" in err or "runtime error" in err: key = "CRASH " + err.strip().split("\n")[0][:100]
        elif ox != ux: key = "errors: oil %s / ours %s" % (ox or "actions", ux or "actions")
        elif sorted(o.split("\n")) == sorted(u.split("\n")): key = "same actions, other order"
        else: key = "other actions"
    groups[key].append(("".join(c), err))
for k, v in sorted(groups.items(), key=lambda kv: -len(kv[1])):
    print("== %d: %s" % (len(v), k))
    if "-v" in sys.argv:
        for c, e in v[:2]: print(c); print(e)
print("%d cases, %d differ" % (len(cases), sum(len(v) for v in groups.values())))
