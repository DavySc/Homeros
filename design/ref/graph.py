import os, sys, time, json
sys.path.insert(0, "/opt/ne/ref"); from drive import *
ed = sys.argv[1]; f = "/opt/ne/ref/w/g.txt"
line = "a" + "é" + "\U0001F468‍\U0001F469‍\U0001F467" + "\U0001F1F3\U0001F1F1" + "中" + "b\n"
out = {}
for n in range(1, 5):
    open(f, "w").write(line)
    env = {"HOME": f"/opt/ne/ref/home-{ed}", "TERM": "xterm-256color"}
    cmd = {"nvim": f"nvim -u NONE -i NONE -n {f}", "helix": f"hx {f}", "emacs": f"emacs -nw -Q {f}"}[ed]
    start(cmd, env); time.sleep(1.5)
    for _ in range(n):
        keys("C-f" if ed == "emacs" else "l"); time.sleep(0.1)
    if ed == "emacs": keys("C-d"); keys("C-x"); keys("C-s")
    else: keys("x" if ed == "nvim" else "d"); keys(":w", lit=True); keys("Enter")
    time.sleep(0.8); tm("kill-server")
    got = open(f).read()
    # which grapheme was deleted?
    deleted = [c for c in ["a", "é", "\U0001F468‍\U0001F469‍\U0001F467", "\U0001F1F3\U0001F1F1", "中", "b"] if c not in got]
    out[n] = {"deleted": [d.encode('unicode_escape').decode() for d in deleted], "left_bytes": len(got.encode())}
print(ed, json.dumps(out))
