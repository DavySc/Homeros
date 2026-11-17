"""Drive an editor in tmux with timed keys; record LSP traffic, file states, screens."""
import json, os, subprocess, sys, time
T = ""
def tm(*a): return subprocess.run(["tmux", "-L", "ref", *a], capture_output=True, text=True).stdout
def keys(*k, lit=False): tm("send-keys", "-t", "e", *(["-l"] if lit else []), *k)
def screen(): return tm("capture-pane", "-p", "-t", "e")
def typ(s, gap=0.05):
    for c in s: keys(c, lit=True); time.sleep(gap)
def start(cmd, env):
    tm("kill-server"); time.sleep(0.2)
    tm("new-session", "-d", "-s", "e", "-x", "100", "-y", "30", "-e", "PATH=" + T + ":" + os.environ["PATH"],
       *sum((["-e", f"{k}={v}"] for k, v in env.items()), []), cmd)
