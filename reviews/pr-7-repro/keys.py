#!/usr/bin/env python3
"""Send KEYS in one write to the editor in a pty, then Esc :wq, print the file.
usage: keys.py HOMEROS_BINARY [-t] KEYS(python bytes literal body)"""
import os, pty, sys, time, tempfile, select
argv = sys.argv[1:]
binary = argv[0]; flags = argv[1:-1]
keys = argv[-1].encode().decode("unicode_escape").encode("latin1")
d = tempfile.mkdtemp(); path = os.path.join(d, "f"); open(path, "w").close()
pid, fd = pty.fork()
if pid == 0: os.execv(binary, [binary] + flags + [path])
def quiet(t):
    last = time.time()
    while time.time() - last < t:
        if select.select([fd], [], [], 0.05)[0]:
            try: os.read(fd, 65536); last = time.time()
            except OSError: return
select.select([fd], [], [], 5); time.sleep(0.3)
os.write(fd, keys); quiet(0.5)
os.write(fd, b"\x1b:wq\r"); quiet(0.5)
print(repr(open(path).read()))
