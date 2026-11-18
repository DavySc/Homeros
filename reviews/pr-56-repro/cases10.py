# second pass: two of the PR's 25 known cases again, with the status line and the screen
# redrawn from insert mode (CTRL-O :redraw! / :redrawstatus), to see whether Neovim was stale
import sys
def load(n):
    c, lines, toks = 0, [], []
    for l in open(TRACE, encoding='utf-8'):
        l = l.rstrip('\n')
        if l.startswith('C '): c += 1
        if c == n:
            if l.startswith('L '): lines.append(l[2:])
            if l.startswith('T '): toks.append(l[2:])
    return lines, toks
import os
TRACE = os.environ['TRACE']   # tests/test_core_markdown.txt of ad620a8
CASES = []
for n in (29, 489, 650):
    lines, toks = load(n)
    CASES.append(('PR case %d as recorded' % n, 20, 50, lines, toks))
    CASES.append(('PR case %d + CTRL-O :redraw!' % n, 20, 50, lines, toks + ['k:\x0f:redraw!\r']))
    CASES.append(('PR case %d + Esc' % n, 20, 50, lines, toks + ['k:<Esc>']))
