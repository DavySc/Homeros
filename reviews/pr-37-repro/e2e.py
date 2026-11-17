#!/usr/bin/env python3
"""Delete through the terminal: the same bytes, with the same pauses, typed
into Homeros and into nvim (-u NONE -n), each on its own pty; the saved files
must be equal.
usage: e2e.py ./homeros [runs]
A case is a list of chunks; (bytes, pause in s after them)."""
import os, pty, select, sys, tempfile, time

E = b'\x1b'
D = E + b'[3~'
TEXT = 'abcdefgh\nsecond line\n\nfourth\n'
S, L = 0.01, 0.25   # inside one key (under 'ttimeoutlen' 50 ms), and past it
CASES = [
 ('Delete', [(D, L)]),
 ('Delete twice in one write', [(D + D, L)]),
 ('3 Delete (count)', [(b'3' + D, L)]),
 ('12 Delete x (Delete drops a count digit)', [(b'12' + D + b'x', L)]),
 ('$ Delete Delete (end of line)', [(b'$' + D + D, L)]),
 ('jj Delete (empty line)', [(b'jj' + D, L)]),
 ('insert: i Delete Esc', [(b'i' + D + E, L)]),
 ('insert: A Delete Esc (joins)', [(b'A' + D + E, L)]),
 ('insert: G A Delete Esc (last line)', [(b'GA' + D + E, L)]),
 ('insert: i x Delete y Esc u', [(b'ix' + D + b'y' + E, L), (b'u', L)]),
 ('Delete u', [(D, L), (b'u', L)]),
 ('cut ESC | [3~ short', [(E, S), (b'[3~', L)]),
 ('cut ESC[ | 3~ short', [(E + b'[', S), (b'3~', L)]),
 ('cut ESC[3 | ~ short', [(E + b'[3', S), (b'~', L)]),
 ('byte by byte short', [(E, S), (b'[', S), (b'3', S), (b'~', L)]),
 ('insert: cut ESC[3 | ~ short', [(b'i', L), (E + b'[3', S), (b'~', L), (E, L)]),
 ('Ctrl-Delete CSI 3;5~', [(E + b'[3;5~', L)]),
 ('Shift-Delete CSI 3;2~', [(E + b'[3;2~', L)]),
 ('Alt-Delete CSI 3;3~', [(E + b'[3;3~', L)]),
 ('insert: Ctrl-Delete', [(b'i' + E + b'[3;5~' + E, L)]),
 ('insert: Shift-Delete', [(b'i' + E + b'[3;2~' + E, L)]),
 ('kitty Delete with event type CSI 3;1:1~', [(E + b'[3;1:1~', L)]),
 ('Esc then Delete, Esc past the wait', [(b'i', L), (E, L), (D, L)]),
 ('insert: Esc Delete in one write', [(b'ix' + E + D, L)]),
 ('Delete then other tilde keys 5~ 6~ (PageUp/Down)', [(D + E + b'[5~' + E + b'[6~' + D, L)]),
 ('arrows around Delete', [(E + b'[C' + D + E + b'[B' + D + E + b'[D' + D, L)]),
 ('the keys a slow Delete falls into: [3 then tilde', [(b'[3', L), (b'~', L)]),
 ('tilde alone (toggle case: not in the reference keys yet)', [(b'~', L)]),
 # slower than the wait: both take the bytes one by one
 ('cut ESC | [3~ long', [(E, L), (b'[3~', L)]),
 ('cut ESC[ | 3~ long', [(E + b'[', L), (b'3~', L)]),
 ('cut ESC[3 | ~ long', [(E + b'[3', L), (b'~', L)]),
 ('insert: cut ESC[3 | ~ long', [(b'i', L), (E + b'[3', L), (b'~', L), (E, L)]),
]

def run(argv, chunks):
    path = tempfile.mktemp(suffix='.txt')
    open(path, 'w').write(TEXT)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm-256color'
        os.execvp(argv[0], argv + [path])
    def drain(t):
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.005)[0]:
                try: os.read(fd, 65536)
                except OSError: return
    select.select([fd], [], [], 5)   # raw mode flushes input: wait for the first frame
    drain(0.5)
    for b, pause in chunks + [(E, L), (b':wq\r', L)]:
        os.write(fd, b); drain(pause)
    try: os.waitpid(pid, 0)
    except ChildProcessError: pass
    s = open(path).read()
    os.unlink(path)
    return s

runs = int(sys.argv[2]) if len(sys.argv) > 2 else 1
bad = 0
for name, chunks in CASES:
    res = set()
    for _ in range(runs):
        h = run([sys.argv[1]], chunks)
        v = run(['nvim', '-u', 'NONE', '-n'], chunks)
        res.add((h, v))
    for h, v in res:
        ok = h == v
        bad += not ok
        print('%-5s %s%s' % ('same' if ok else 'DIFF', name, '' if ok else '\n      homeros %r\n      nvim    %r' % (h, v)))
print('%d cases, %d runs each: %d differ' % (len(CASES), runs, bad))
