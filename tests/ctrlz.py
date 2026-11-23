#!/usr/bin/env python3
"""Ctrl+Z in the terminal goes to its shell, not to main.c's suspend (PR 94's
review, finding 1): the editor under headless sway, keys from wtype, a job
stopped with Ctrl+Z and listed by `jobs` into a file.  Ghostty, snacks and
Neovim's :terminal give Ctrl+Z to the shell too.
usage: ctrlz.py HOMEROS_BINARY; exit 1 on failure"""
import os, signal, subprocess, sys, tempfile, time
binary = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp()
run = os.path.join(d, "run")
os.mkdir(run, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_STATE_HOME=d, XDG_DATA_HOME=d, HOME=d,
           SHELL="/bin/sh", ENV="/dev/null", PS1="$ ")
open(os.path.join(d, "sway.cfg"), "w").write("output HEADLESS-1 resolution 1280x800 scale 1\ndefault_border none\n")
sway = subprocess.Popen(["sway", "-c", os.path.join(d, "sway.cfg")], cwd=d, start_new_session=True,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        env=dict(env, WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1"))
procs = [sway]
try:
    for _ in range(100):
        sock = [f for f in os.listdir(run) if f.startswith("wayland-") and not f.endswith(".lock")]
        if sock: break
        time.sleep(0.1)
    env["WAYLAND_DISPLAY"] = sock[0]
    # a keyboard for the whole test, so the seat never loses it (test_wl.c)
    procs.append(subprocess.Popen(["wtype", "-s", "600000"], env=env, start_new_session=True))
    time.sleep(0.2)
    procs.append(subprocess.Popen([binary, "x.txt"], cwd=d, env=env, start_new_session=True,
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    def keys(*a): subprocess.run(["wtype"] + list(a), env=env, check=True)
    def wait(f, what):
        end = time.time() + 10
        while time.time() < end and not (os.path.exists(f) and what in open(f).read()): time.sleep(0.1)
        return os.path.exists(f) and what in open(f).read()
    time.sleep(2)
    keys("-M", "ctrl", "-k", "period", "-m", "ctrl", "-s", "500", "echo up > u", "-k", "Return")
    if not wait(os.path.join(d, "u"), "up"):
        print("FAIL ctrlz: the terminal did not open"); sys.exit(1)
    keys("sleep 300", "-k", "Return", "-s", "500", "-M", "ctrl", "z", "-m", "ctrl", "-s", "500", "jobs > j", "-k", "Return")
    if not wait(os.path.join(d, "j"), "Stopped"):
        print("FAIL ctrlz: Ctrl+Z in the terminal did not stop the shell's job"); sys.exit(1)
    print("ctrlz: Ctrl+Z in the terminal stops the shell's job")
finally:
    for p in reversed(procs):
        try: os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError: pass
