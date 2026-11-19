"""The editor in a pty, for the tests that drive the binary (hup.py,
paste.py, start.py)."""
import os, pty, select, time

def spawn(argv):
    """runs argv in a pty and waits until it is up (entering raw mode flushes
    input); returns (pid, fd)"""
    pid, fd = pty.fork()
    if pid == 0: os.execv(argv[0], argv)
    select.select([fd], [], [], 5); time.sleep(0.3)
    return pid, fd

def drain(fd, t):
    """what the editor writes in t seconds, or until it closes the pty"""
    out, end = b"", time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.1)[0]:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out
