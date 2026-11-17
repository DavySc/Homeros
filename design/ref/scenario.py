import json, os, sys, time
sys.path.insert(0, os.path.dirname(__file__)); from drive import *
ed = sys.argv[1]; W = "/opt/ne/ref/w"; os.makedirs(W, exist_ok=True)
f = f"{W}/t.fake"; open(f, "w").write("line1\n")
log = f"/opt/ne/ref/out/{ed}.lsp.jsonl"; os.makedirs("/opt/ne/ref/out", exist_ok=True)
open(log, "w").close()
env = {"FAKELSP_LOG": log, "HOME": f"/opt/ne/ref/home-{ed}", "TERM": "xterm-256color"}
os.makedirs(env["HOME"] + "/.config", exist_ok=True)
if ed == "helix":
    os.system(f"rm -rf {env['HOME']}/.config/helix; cp -r cfg/helix {env['HOME']}/.config/helix")
cmd = {"nvim": f"nvim -u /opt/ne/ref/cfg/init.lua -i NONE -n {f}",
       "helix": f"hx {f}",
       "emacs": f"emacs -nw -Q -l /opt/ne/ref/cfg/emacs.el {f}"}[ed]
res = {}
def mark(n): open(log, "a").write(json.dumps({"mark": n, "t": time.time()}) + "\n")
def save():
    if ed == "emacs": keys("C-x"); keys("C-s")
    else: keys(":w", lit=True); keys("Enter")
    time.sleep(0.8)
start(cmd, env); time.sleep(3)
ino0 = os.stat(f).st_ino
# E1: type "abc" 50 ms apart at end of line 1
mark("E1 type abc")
if ed == "emacs": keys("C-e")
else: keys("A", lit=True)
typ("abc"); time.sleep(1.5)
if ed != "emacs": keys("Escape")
time.sleep(0.5)
# E2: hover, then edit 200 ms later
mark("E2 hover then edit")
if ed == "nvim": keys("K")
elif ed == "helix": keys("Space"); keys("k", lit=True)
else: keys("M-x"); typ("eldoc", 0.01); keys("Enter")
time.sleep(0.2)
if ed == "emacs": typ("x")
else: keys("A", lit=True); typ("x"); keys("Escape")
time.sleep(3)
# E3: undo granularity: type "hello world" in one insert session, then undo once
mark("E3 undo")
save()
if ed == "emacs": keys("C-e"); keys("Enter"); typ("hello world"); time.sleep(0.3); keys("C-_")
else: keys("o", lit=True); typ("hello world"); keys("Escape"); time.sleep(0.3); keys("u", lit=True)
time.sleep(0.5); save()
res["after_one_undo"] = open(f).read()
res["inode_changed_on_save"] = os.stat(f).st_ino != ino0
# E5: external change, then edit + save
mark("E5 external change")
time.sleep(1.1); open(f, "a").write("EXTERNAL\n")
time.sleep(0.5)
if ed == "emacs": typ("y")
else: keys("A", lit=True); typ("y"); keys("Escape")
time.sleep(0.5); save(); time.sleep(1)
res["screen_after_save_over_external"] = [l for l in screen().splitlines() if l.strip()][-3:]
res["file_after"] = open(f).read()
tm("kill-server")
msgs = [json.loads(l) for l in open(log)]
res["lsp"] = msgs
json.dump(res, open(f"/opt/ne/ref/out/{ed}.json", "w"), indent=1)
print(json.dumps({k: v for k, v in res.items() if k != "lsp"}, indent=1))
for m in msgs:
    if "mark" in m: print("--", m["mark"])
    else:
        print(m["ms"], m["method"], m.get("id") or "", json.dumps(m.get("contentChanges", ""))[:120], (m.get("textDocument") or {}).get("version", ""))
