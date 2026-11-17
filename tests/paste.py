#!/usr/bin/env python3
"""Paste N bytes of numbered lines into the editor through a pty in insert
mode, wait until it is idle, then Esc :wq, and compare the file: a paste
larger than the key queue must lose nothing (from the review of PR 7).
usage: paste.py HOMEROS_BINARY [-t] N; exit 1 if text was lost"""
import os, pty, sys, time, tempfile, select
argv = sys.argv[1:]
binary = argv[0]; n = int(argv[-1]); flags = argv[1:-1]
d = tempfile.mkdtemp(); path = os.path.join(d, "f")
open(path, "w").close()
pid, fd = pty.fork()
if pid == 0:
    os.execv(binary, [binary] + flags + [path])
def send(data, quiet):
    """write data, reading the screen meanwhile; return once nothing came for quiet s"""
    i = 0; last = time.time()
    while i < len(data) or time.time() - last < quiet:
        r, w, _ = select.select([fd], [fd] if i < len(data) else [], [], 0.1)
        if r:
            try: os.read(fd, 65536); last = time.time()
            except OSError: return
        if w: i += os.write(fd, data[i:i + 256]); last = time.time()
select.select([fd], [], [], 5); time.sleep(0.3)   # the editor is up (entering raw mode flushes input)
text = "".join("%07d\n" % i for i in range(n // 8))
send(b"i" + text.encode().replace(b"\n", b"\r"), 1.5)
send(b"\x1b:wq\r", 1.0)
got = open(path).read()
want = text + "\n"          # the last Enter opens one more, empty, line
lines = got.count("\n")
print("pasted %d bytes (%d lines); file has %d bytes (%d lines): %s"
      % (len(text), n // 8, len(got), lines, "all there" if got == want else "TEXT LOST"))
sys.exit(0 if got == want else 1)
