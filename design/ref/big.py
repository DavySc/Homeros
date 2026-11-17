import os, sys, time, subprocess
sys.path.insert(0, "/opt/ne/ref"); from drive import *
ed = sys.argv[1]; f = "/opt/ne/proto/big.txt"
cmd = {"nvim": f"nvim -u NONE -i NONE -n {f}", "helix": f"hx {f}", "emacs": f"emacs -nw -Q {f}"}[ed]
env = {"HOME": f"/opt/ne/ref/home-{ed}", "TERM": "xterm-256color"}
t = time.time(); start(cmd, env)
if ed == "emacs":
    while "really open" not in screen(): time.sleep(0.02)
    keys("y", lit=True)
while not any(w in screen() for w in ("theorem","lemma","calc","rfl")):
    time.sleep(0.02)
    if time.time()-t>50: print(ed,"timeout", screen()[:300]); break
t1 = time.time() - t
time.sleep(1)
pid = tm("list-panes", "-t", "e", "-F", "#{pane_pid}").strip()
def rss(p):
    try: return int([l for l in open(f"/proc/{p}/status") if l.startswith("VmRSS")][0].split()[1]) // 1024
    except Exception: return 0
kids = subprocess.run(["pgrep", "-P", pid], capture_output=True, text=True).stdout.split()
tot = rss(pid) + sum(rss(k) for k in kids)
# jump to end and back, time until screen changes
s0 = screen(); t = time.time(); keys("G" if ed != "emacs" else "M->")
while screen() == s0 and time.time()-t < 30: time.sleep(0.01)
t2 = time.time() - t
tm("kill-server")
print(f"{ed:6} open_100MB={t1:.2f}s rss={tot}MB jump_to_end={t2*1000:.0f}ms")
