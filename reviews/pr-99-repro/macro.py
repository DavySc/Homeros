#!/usr/bin/env python3
"""Review of PR 99: the same key steps typed into the real editor
(build/san/homeros -t) and into Neovim running my-nvim's kitten.lua, each in
a 24 x 80 pty; after every step, did a kitten play?  Homeros: the cat's ears
" /\\_/\\ " appear in the terminal output of that step.  Neovim: kitten.lua's
M.paw was called (a wrapper appends a line to a file).
usage: [HOMEROS=build/gcc/homeros] KITTEN=path/to/my-nvim/lua/config/core/kitten.lua macro.py   (from the repo root)
Prints one line per step and exits 1 if the two differ anywhere."""
import fcntl, os, pty, select, struct, sys, tempfile, termios, time

STEPS = [  # (what, keys)
    ('typed :w', ':w\r'),
    ('record qq:w<CR> (the save is typed)', 'qq:w\r'),
    ('q ends the recording', 'q'),
    ('@q', '@q'),
    ('typed :w after the macro', ':w\r'),
    ('@@', '@@'),
    ('3@q', '3@q'),
    ('@: (repeats :w)', '@:'),
    ('record qw:w<CR>', 'qw:w\r'),
    ('jq', 'jq'),
    ('@w (keys follow the save)', '@w'),
    ('record qeix<Esc>:w<CR>', 'qeix\x1b:w\r'),
    ('q', 'q'),
    ('@e', '@e'),
    ('typed :w at the end', ':w\r'),
]

def spawn(argv, d):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm-256color'
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        os.dup2(os.open(os.devnull, os.O_WRONLY), 2)
        os.execvp(argv[0], argv)
    return pid, fd

def drain(fd, t):
    end, out = time.time() + t, b''
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out

def homeros():
    d = tempfile.mkdtemp()
    open(os.path.join(d, 'notes.txt'), 'w').write('one\ntwo\nthree\nfour\nfive\nsix\n')
    pid, fd = spawn([os.environ.get('HOMEROS', 'build/san/homeros'), '-t', os.path.join(d, 'notes.txt')], d)
    drain(fd, 1.5)
    res = []
    for what, keys in STEPS:
        os.write(fd, keys.encode())
        out = drain(fd, 3.5)          # the kitten plays 2.2 s
        res.append(b'/\\_/\\' in out)
    os.kill(pid, 9)
    return res

def nvim():
    d = tempfile.mkdtemp()
    open(os.path.join(d, 'notes.txt'), 'w').write('one\ntwo\nthree\nfour\nfive\nsix\n')
    log = os.path.join(d, 'paws')
    init = os.path.join(d, 'init.lua')
    open(log, 'w').close()
    open(init, 'w').write('''
vim.o.swapfile = false
local K = dofile(vim.env.KITTEN)
local paw = K.paw
K.paw = function(...) local f = io.open(%r, 'a') f:write('paw\\n') f:close() return paw(...) end
K.setup()
''' % log)
    pid, fd = spawn(['nvim', '--clean', '-u', init, os.path.join(d, 'notes.txt')], d)
    drain(fd, 1.5)
    ns = [0]
    for what, keys in STEPS:
        os.write(fd, keys.encode())
        drain(fd, 3.5)
        ns.append(len(open(log).readlines()))
    os.kill(pid, 9)
    return [b > a for a, b in zip(ns, ns[1:])]

h, v = homeros(), nvim()
bad = 0
for (what, _), a, b in zip(STEPS, h, v):
    print('%-40s homeros %-6s nvim %-6s %s' % (what, 'kitten' if a else '-', 'kitten' if b else '-', '' if a == b else 'DIFFER'))
    bad += a != b
sys.exit(1 if bad else 0)
