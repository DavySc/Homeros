#!/usr/bin/env python3
"""Review of PR 56: records named cases from live Neovim with render-markdown
for mdcmp.c.  usage: RM_RTP=render-markdown.nvim:catppuccin rec.py CASES.py [INIT.lua] > FILE
CASES.py defines CASES = [(name, rows, cols, [lines], [tokens]), ...].
As tests/test_core_markdown.py (tmux, pyte), but any command, every row
but the command line, and the terminal's cursor."""
import os, subprocess, sys, time, pyte, concurrent.futures, runpy
CASES = runpy.run_path(sys.argv[1])['CASES']
# HOMEROS=binary: the editor itself in the terminal instead of Neovim, for the cases made of keys only
HOMEROS = os.environ.get('HOMEROS')
if HOMEROS: CASES = [c for c in CASES if all(t.startswith('k:') for t in c[4])]
HERE = os.path.dirname(os.path.abspath(__file__))
INIT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '../../tests/test_core_markdown.lua')
BG = {'5a3d50': 'R', '5c4847': 'P', '5b5552': 'Y', '44554e': 'G', '364d63': 'S', '484b68': 'L'}
FG = {'f38ba8': 'r', 'fab387': 'p', 'f9e2af': 'y', 'a6e3a1': 'g', '74c7ec': 's', 'b4befe': 'l'}

def nvkeys(t):
    if t.startswith('k:'): return t[2:].replace('<Esc>', '\x1b')
    if t in ('wrap', 'nowrap'): return ':set %s\r' % t
    if t in ('sp', 'vs'): return ':%s\r' % t
    if t.startswith('w:'): return '\x17' + t[2]
    if t == 'C-e': return '\x05'
    if t == 'C-y': return '\x19'
    if t == 'visual': return 'v'
    if t == 'redraw': return '\x0c'   # CTRL-L: Neovim's screen can be stale after an extmark goes
    if t.startswith('e:'): return ':e %s\r' % t[2:]
    raise ValueError(t)

def case(ic):
    i, (name, H, W, lines, toks) = ic
    d = '/tmp/rmr%d_%d' % (os.getpid(), i)
    os.makedirs(d, exist_ok=True)
    open(d + '/f.md', 'w').write('\n'.join(lines) + '\n')
    tm = lambda *a: subprocess.run(['tmux', '-L', 'rmr%d' % i, *a], capture_output=True, text=True).stdout
    tm('kill-server')
    tm('new-session', '-d', '-x', str(W), '-y', str(H), '-c', d, '%s f.md' % HOMEROS if HOMEROS else 'nvim -u %s f.md' % INIT)
    time.sleep(1.5)
    for t in toks:
        tm('send-keys', '-l', nvkeys(t))
        time.sleep(0.7)
    time.sleep(0.5)
    raw = tm('capture-pane', '-e', '-p', '-N')
    cur = tm('display-message', '-p', '#{cursor_y} #{cursor_x}').strip()
    tm('kill-server')
    scr = pyte.Screen(W, H)
    pyte.Stream(scr).feed(raw.rstrip('\n').replace('\n', '\r\n'))
    out = ['C %d %d %s' % (H, W, name)] + ['L ' + l for l in lines] + ['T ' + t for t in toks] + ['X ' + cur]
    for y in range(H - 1):
        row = scr.buffer[y]
        out.append('S ' + ''.join(row[x].data for x in range(W)))
        out.append('B ' + ''.join(BG.get(row[x].bg, '.') for x in range(W)))
        out.append('F ' + ''.join('.' if row[x].data == ' ' else FG.get(row[x].fg, '.') for x in range(W)))
    return '\n'.join(out + ['E'])

with concurrent.futures.ThreadPoolExecutor(6) as ex:
    for c in ex.map(case, enumerate(CASES)): print(c)
