#!/usr/bin/env python3
"""Records render-markdown's headings for tests/test_core_markdown.c.
usage: RM_RTP=render-markdown.nvim:catppuccin-nvim test_core_markdown.py N SEED > FILE
Makes N random Markdown files and key strings, runs each in a 50x20 tmux in
Neovim with tests/test_core_markdown.lua (render-markdown has no headless
output: what it draws is only on a screen) and writes, per case, the file's
lines, the keys and the window rows: their text, their background (a letter
per heading level, '.' otherwise) and their foreground (the same for the
rainbow colours of headings; blanks '.').  Needs nvim 0.12, tmux and python's pyte."""
import os, random, subprocess, sys, time, pyte, concurrent.futures
N, SEED = int(sys.argv[1]), int(sys.argv[2])
W, H = 50, 20
INIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_core_markdown.lua')
# catppuccin's RenderMarkdownH1Bg..H6Bg and rainbow1..6, as ref/theme.c
BG = {'32283a': 'R', '332c36': 'P', '33313a': 'Y', '2b3139': 'G', '262e40': 'S', '2c2d42': 'L'}
FG = {'f38ba8': 'r', 'fab387': 'p', 'f9e2af': 'y', 'a6e3a1': 'g', '74c7ec': 's', 'b4befe': 'l'}
WORDS = ['foo', 'bar', '`code`', '[link](u)', '*em*', '**strong**', '漢字', 'x' * 60, '#', '##', '\t', 'é', '<b>', '\\#']

def line(r):
    k = r.random()
    if k < 0.45:   # headings and near misses: indent, containers, 7 '#', no blank, closing '#'
        # (22 '> ' put the marker at the window's edge)
        ind = r.choice(['', '', '', ' ', '  ', '   ', '    ', '> ', '- ', '1. ', '> > ', '> ' * 22])
        after = r.choice([' ', ' ', ' ', '\t', '', 'x', '  '])
        tail = ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 4)))
        return ind + '#' * r.choice([1, 1, 2, 2, 3, 4, 5, 6, 7]) + after + tail + r.choice(['', '', ' #', ' ###', '#'])
    if k < 0.75: return ' '.join(r.choice(WORDS) for _ in range(r.randint(1, 6)))
    if k < 0.85: return ''
    if k < 0.9: return r.choice(['```', '```c', '~~~'])
    # setext underlines, so a paragraph starting with '#' gets a heading face
    # (not drawn here yet, nor by the .lua's render-markdown)
    return r.choice(['- item', '> quote', '    indented code # x', '<!-- # c -->', '===', '---'])

def case(i):
    r = random.Random(SEED * 100000 + i)
    lines = [line(r) for _ in range(r.randint(3, 14))]
    # a cursor line, then normal, insert, command-line mode or the line's end
    # (a long line scrolls the window sideways)
    move, mode = '%dG' % r.randint(1, len(lines)), r.choice(['', '', '', '', 'i', ':', '$'])
    keys = move + mode
    d = '/tmp/rm%d_%d' % (os.getpid(), i)
    os.makedirs(d, exist_ok=True)
    open(d + '/f.md', 'w').write('\n'.join(lines) + '\n')
    tm = lambda *a: subprocess.run(['tmux', '-L', 'rm%d' % i, *a], capture_output=True, text=True).stdout
    tm('kill-server')
    tm('new-session', '-d', '-x', str(W), '-y', str(H), '-c', d, 'nvim -u %s f.md' % INIT)
    time.sleep(1.2)
    # the mode key once render-markdown has drawn after the move: it
    # redraws on a mode change but not on entering the command line, so a
    # ':' typed within its debounce (100 ms) shows the cursor line rendered
    tm('send-keys', '-l', move)
    time.sleep(0.5)
    if mode: tm('send-keys', '-l', mode)
    time.sleep(0.8)
    raw = tm('capture-pane', '-e', '-p', '-N')
    tm('kill-server')
    scr = pyte.Screen(W, H)
    pyte.Stream(scr).feed(raw.rstrip('\n').replace('\n', '\r\n'))
    out = ['C %d %d' % (H, W)] + ['L ' + l for l in lines] + ['K ' + keys]
    for y in range(H - 2):   # not the status and command lines
        row = scr.buffer[y]
        out.append('S ' + ''.join(row[x].data for x in range(W)))
        out.append('B ' + ''.join(BG.get(row[x].bg, '.') for x in range(W)))
        # a blank's foreground does not show (and Neovim may not send one)
        out.append('F ' + ''.join('.' if row[x].data == ' ' else FG.get(row[x].fg, '.') for x in range(W)))
    return '\n'.join(out + ['E'])

with concurrent.futures.ThreadPoolExecutor(8) as ex:
    for c in ex.map(case, range(N)): print(c)
