import os, sys, time, subprocess
sys.path.insert(0, "/opt/ne/ref"); from drive import *
ed = sys.argv[1]; f = "/opt/ne/ref/w/g.txt"; open(f, "w").write("hello\n")
cmd = {"nvim": f"nvim -u NONE -i NONE -n {f}", "helix": f"hx {f}", "emacs": f"emacs -nw -Q {f}"}[ed]
start(cmd, {"HOME": f"/opt/ne/ref/home-{ed}", "TERM": "xterm-256color"}); time.sleep(3)
pid = tm("list-panes", "-t", "e", "-F", "#{pane_pid}").strip()
def procs(p):
    out = [p]; 
    for k in subprocess.run(["pgrep", "-P", p], capture_output=True, text=True).stdout.split(): out += procs(k)
    return out
def stat():
    sw = cpu = 0
    for p in procs(pid):
        for t in os.listdir(f"/proc/{p}/task"):
            s = open(f"/proc/{p}/task/{t}/status").read()
            sw += sum(int(l.split()[1]) for l in s.splitlines() if "ctxt_switches" in l)
            a = open(f"/proc/{p}/task/{t}/stat").read().rsplit(")", 1)[1].split(); cpu += int(a[11]) + int(a[12])
    return sw, cpu
s0, c0 = stat(); time.sleep(20); s1, c1 = stat()
tm("kill-server")
print(f"{ed:6} idle 20s: context switches={s1-s0}  cpu_ticks={c1-c0}  threads={sum(len(os.listdir(f'/proc/{p}/task')) for p in procs(pid)) if False else 'n/a'}")
