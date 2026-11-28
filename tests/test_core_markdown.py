#!/usr/bin/env python3
"""Records render-markdown for tests/test_core_markdown.c.
usage: RM_RTP=render-markdown.nvim:catppuccin-nvim test_core_markdown.py N SEED [CASES.py] > FILE
(the checked-in file: N 1000, SEED 1, tests/test_core_markdown_cases.py:
the review of PR 56's cases and more: CASES = [(name, rows, cols, [lines], [commands])])
With --float the cases open the LSP float (vim.lsp.util.open_floating_preview,
Markdown) over f.md, lines P, then maybe move or change mode
(tests/test_core_float.txt: N 300, SEED 1, then CASES.py's FLOAT_CASES,
whose sixth item is the float's lines).
Makes N random Markdown files (f.md, and g.md for a second buffer) and
commands (T: k:keys, wrap, sp, vs, w:<c> for CTRL-W <c>, e:file, visual),
runs each in a 50x20 tmux in Neovim with tests/test_core_markdown.lua
(render-markdown has no headless output: what it draws is only on a screen)
and writes, per case, the files' lines (L, M), the commands, the cursor (X)
and the rows but the command line: their text, their background (a letter
per heading level, C for the cursor line, M for code's mantle; '.' otherwise)
and their foreground (the rainbow colours of headings, b the bullets'
sky, u an unchecked box's overlay1, c mantle; blanks '.').  Needs nvim 0.12, tmux and python's pyte."""
import json, os, random, runpy, subprocess, sys, time, pyte, concurrent.futures
FLOAT = '--float' in sys.argv
if FLOAT: sys.argv.remove('--float')
N, SEED = int(sys.argv[1]), int(sys.argv[2])
W, H = 50, 20
CASES = runpy.run_path(sys.argv[3])['FLOAT_CASES' if FLOAT else 'CASES'] if len(sys.argv) > 3 else []
INIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_core_markdown.lua')
# catppuccin's RenderMarkdownH1Bg..H6Bg, CursorLine, mantle, rainbow1..6, as ref/theme.c
BG = {'32283a': 'R', '332c36': 'P', '33313a': 'Y', '2b3139': 'G', '262e40': 'S', '2c2d42': 'L', '2a2b3c': 'C', '181825': 'M'}
FG = {'f38ba8': 'r', 'fab387': 'p', 'f9e2af': 'y', 'a6e3a1': 'g', '74c7ec': 's', 'b4befe': 'l',
      '89dceb': 'b', '7f849c': 'u', '181825': 'c'}
WORDS = ['foo', 'bar', '`code`', '[link](u)', '*em*', '**strong**', '漢字', 'x' * 60, '#', '##', '\t', 'é', '<b>', '\\#']

def line(r):
    k = r.random()
    if k < 0.45:   # headings and near misses: indent, containers, 7 '#', no blank, closing '#'
        # (22 '> ' put the marker at the window's edge)
        ind = r.choice(['', '', '', ' ', '  ', '   ', '    ', '> ', '- ', '1. ', '> > ', '> ' * 22, '%d%s ' % (r.randint(1, 20), r.choice('.)')), '  %d. ' % r.randint(7, 9)])
        after = r.choice([' ', ' ', ' ', '\t', '', 'x', '  '])
        tail = ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 4)))
        return ind + '#' * r.choice([1, 1, 2, 2, 3, 4, 5, 6, 7]) + after + tail + r.choice(['', '', ' #', ' ###', '#'])
    if k < 0.6:    # list items: nesting, ordered numbers, checkboxes and near misses
        ind = r.choice(['', '', '  ', '    ', '      ', '> ', '\t'])
        mark = r.choice(['-', '*', '+', '-', '1.', '2.', '0.', '7)', '%d.' % r.randint(1, 120), '1.'])
        box = r.choice(['', '', '', '[ ] ', '[x] ', '[X] ', '[-] ', '[-]', '[ ]x ', '[?] '])
        return ind + mark + r.choice([' ', ' ', '  ', '']) + box + ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 3)))
    if k < 0.7:    # fenced code blocks: info strings, indent, containers, empty and unclosed
        ind = r.choice(['', '', '', '  ', '> ', '- ', '   '])
        f = r.choice(['```', '```', '~~~', '````'])
        info = r.choice(['', 'c', 'lua', 'sql', 'c {.x}', ' python', 'x' * 45, 'a`b', '漢字'])
        body = [ind + r.choice(WORDS + ['', 'int x;', '\tx']) for _ in range(r.choice([0, 1, 2, 3, 20]))]
        return '\n'.join([ind + f + info] + body + ([ind + f] if r.random() < 0.85 else []))
    if k < 0.78: return ' '.join(r.choice(WORDS) for _ in range(r.randint(1, 6)))
    if k < 0.85: return ''
    # a setext heading whose text starts with an inline face, then '#'
    if k < 0.94: return '## ' + ' '.join(['x' * 60] * 10)   # taller than a window: skipcol
    if k < 0.97: return r.choice(['\\#', '`c`', '*em*']) + ' # ' + r.choice(WORDS) + '\n' + r.choice(['===', '---'])
    # setext underlines, so a paragraph starting with '#' gets a heading face
    # (not drawn here yet, nor by the .lua's render-markdown)
    return r.choice(['- item', '> quote', '    indented code # x', '<!-- # c -->', '===', '---'])

def nvkeys(t):
    if t.startswith('k:'): return t[2:]
    if t in ('wrap', 'sp', 'vs'): return ':set wrap\r' if t == 'wrap' else ':%s\r' % t
    if t.startswith('w:'): return '\x17' + t[2]
    if t.startswith('e:'): return ':e %s\r' % t[2:]
    if t == 'redraw': return '\x0c'
    if t == 'pop': return ''   # over RPC, in any mode
    return 'v'   # visual

def case(i):
    r = random.Random(SEED * 100000 + i)
    lines = [x for _ in range(r.randint(3, 14)) for x in line(r).split('\n')]
    other = [x for _ in range(r.randint(3, 8)) for x in line(r).split('\n')]
    n = len(lines)
    # commands: 'wrap'; a split, its cursor line, and maybe another buffer
    # in it (with a CTRL-W there and back: after ':e' render-markdown keeps
    # the old window's cursor line raw until it is entered again); then a
    # cursor line and the mode: normal, insert, command line, visual or
    # the line's end (a long line scrolls the window sideways)
    # '0' first: Neovim opens a file on line 1's first non-blank, the core
    # on column 0 (package 4's, not drawing's)
    toks = ['k:0'] + (['wrap'] if r.random() < 0.5 else [])
    if r.random() < 0.5:
        # (the line's end in one window scrolls it sideways, or with wrap
        # shows a long line from skipcol, while the other is current)
        toks += [r.choice(['sp', 'vs']), 'k:%dG%s' % (r.randint(1, n), r.choice(['', '$']))]
        if r.random() < 0.4: toks += ['e:g.md', 'w:w', 'w:w', 'k:%dG%s' % (r.randint(1, len(other)), r.choice(['', '$']))]
        if r.random() < 0.5: toks += ['w:w']
    toks += ['k:%dG' % r.randint(1, n)] if toks[-1] != 'w:w' or r.random() < 0.5 else []
    mode = r.choice(['', '', '', 'k:i', 'k::', 'k:$', 'visual'])
    pop = []
    if FLOAT:
        # hover's shapes: blank lines in a row, thematic breaks with blanks
        # around them, a code block, long lines that wrap, then Markdown
        pop = [x for _ in range(r.choice([1, 2, 3, 6, 12, 25])) for x in r.choice([
            lambda: line(r), lambda: '', lambda: '', lambda: r.choice(['---', ' ***', '___', '- - -', '--', '---x']),
            lambda: '```%s\n%s\n```' % (r.choice(['', 'c', 'cpp']), r.choice(WORDS)),
            lambda: ' '.join(r.choice(WORDS) for _ in range(r.randint(5, 14)))])().split('\n')]
        mode = r.choice(['', '', 'k:i', 'visual', 'k:l', 'k:j'])
        toks += ['pop']
    toks += [mode] if mode else []
    return record(i, H, W, lines, other, toks, pop=pop)

def record(i, H, W, lines, other, toks, name='', pop=()):
    d = '/tmp/rm%d_%d' % (os.getpid(), i)
    os.makedirs(d, exist_ok=True)
    open(d + '/p.md', 'w').write(''.join(l + '\n' for l in pop))
    open(d + '/f.md', 'w').write('\n'.join(lines) + '\n')
    open(d + '/g.md', 'w').write('\n'.join(other) + '\n')
    tm = lambda *a: subprocess.run(['tmux', '-L', 'rm%d' % i, *a], capture_output=True, text=True).stdout
    tm('kill-server')
    tm('new-session', '-d', '-x', str(W), '-y', str(H), '-c', d, 'nvim -u %s --listen %s/s f.md' % (INIT, d))
    time.sleep(1.2)
    # a command at a time, once render-markdown has drawn after the last:
    # it redraws on a mode change but not on entering the command line, so
    # a ':' typed within its debounce (100 ms) shows the cursor line rendered
    for t in toks:
        if t == 'pop': subprocess.run(['nvim', '--server', d + '/s', '--remote-expr', 'execute("Pop")'], capture_output=True)
        else: tm('send-keys', '-l', nvkeys(t))
        time.sleep(0.5)
    time.sleep(1.0)   # Neovim parses the inline grammar of the lines in view late
    # :redraw! over RPC, which leaves the mode as it is: Neovim leaves windows
    # it has no reason to redraw stale (an extmark visual mode or insert mode
    # took away still shows)
    subprocess.run(['nvim', '--server', d + '/s', '--remote-expr', 'execute("redraw!")'], capture_output=True)
    time.sleep(0.5)
    # the status lines' rows (a float's mantle at column 0 is not one)
    stls = subprocess.run(['nvim', '--server', d + '/s', '--remote-expr', 'luaeval("vim.json.encode(vim.tbl_map(function(w) return vim.fn.win_screenpos(w)[1] + vim.api.nvim_win_get_height(w) - 1 end, vim.tbl_filter(function(w) return vim.api.nvim_win_get_config(w).relative == \'\' end, vim.api.nvim_list_wins())))")'], capture_output=True, text=True).stdout
    # the text as Neovim's grid has it (screenstring()): tmux shows a
    # double-width character at a float's column 0 as two blanks
    # (measured: test_core_float.txt case 21's 漢 at row 2)
    grid = json.loads(subprocess.run(['nvim', '--server', d + '/s', '--remote-expr', 'json_encode(map(range(1, %d), {_, r -> map(range(1, %d), {_, c -> screenstring(r, c)})}))' % (H - 1, W)], capture_output=True, text=True).stdout)
    raw = tm('capture-pane', '-e', '-p', '-N')
    cur = tm('display-message', '-p', '#{cursor_y} #{cursor_x}').strip()
    tm('kill-server')
    scr = pyte.Screen(W, H)
    pyte.Stream(scr).feed(raw.rstrip('\n').replace('\n', '\r\n'))
    out = [('C %d %d %s' % (H, W, name)).rstrip()] + ['L ' + l for l in lines] + ['M ' + l for l in other] + ['P ' + l for l in pop] + ['T ' + t for t in toks] + ['X ' + cur]
    for y in range(H - 1):   # not the command line
        row = scr.buffer[y]
        out.append('S ' + ''.join(grid[y]))
        # a status line (mantle from column 0, where a window's text is
        # after its number column) is not compared: the core styles it as Davy chose
        stl = y in json.loads(stls)
        out.append('B ' + ''.join('.' if stl else BG.get(row[x].bg, '.') for x in range(W)))
        # a blank's foreground does not show (and Neovim may not send one)
        # (a wide character's second cell as its first: tmux's may be a blank)
        out.append('F ' + ''.join('.' if grid[y][x] == ' ' else FG.get(row[x - (x > 0 and grid[y][x] == '')].fg, '.') for x in range(W)))
    return '\n'.join(out + ['E'])

with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for c in ex.map(case, range(N)): print(c)
    for c in ex.map(lambda ic: record(N + ic[0], ic[1][1], ic[1][2], ic[1][3], ['x'], ic[1][4], ic[1][0], *ic[1][5:]), enumerate(CASES)): print(c)
