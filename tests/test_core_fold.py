#!/usr/bin/env python3
"""Closed folds as Neovim 0.12 draws and scrolls them (package 4), for
test_core_fold.c.
usage: test_core_fold.py N [SEED] --record FILE
Each case: a random text, a fold level per line (foldmethod=expr, "N" or
">N"), 'foldlevel' 0, 1 or 99, 'wrap' or not, a screen size, then commands:
a fold closed or opened at a line (:foldclose, :foldopen), zM, zR, the cursor
put on a line (cursor()), and the scrolling keys with counts (CTRL-E CTRL-Y
CTRL-D CTRL-U CTRL-F CTRL-B zt zz zb H M L gj gk).  After each one it
records the window's rows (screenstring()), which cells are Folded (F) or
not (.), the top line and the cursor."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP, gen_text

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
vim.o.lines = c.rows
vim.o.columns = c.cols
LV = c.levels
function FE(l) return LV[l] or '0' end
vim.o.wrap = c.wrap
vim.wo.foldmethod = 'expr'
vim.wo.foldexpr = 'v:lua.FE(v:lnum)'
vim.wo.foldlevel = c.foldlevel
vim.cmd('normal! 1G^')
local function shot()
  vim.cmd('redraw!')
  local info = vim.fn.getwininfo(vim.fn.win_getid())[1]
  local rows, attrs = {}, {}
  for r = 1, info.height do
    local s, a = {}, {}
    for col = 1, info.width do s[#s + 1] = vim.fn.screenstring(r, col) a[#a + 1] = vim.fn.screenattr(r, col) end
    rows[#rows + 1] = table.concat(s)
    attrs[#attrs + 1] = a
  end
  local p = vim.api.nvim_win_get_cursor(0)
  return {rows = rows, attrs = attrs, top = info.topline - 1, line = p[1] - 1, col = p[2]}
end
local out = {shot()}
for _, k in ipairs(c.keys) do
  pcall(vim.api.nvim_feedkeys, vim.api.nvim_replace_termcodes(k, true, false, true), 'xt', false)
  out[#out + 1] = shot()
end
-- Folded's attribute, from a fold made by hand
vim.cmd('enew')
vim.wo.foldmethod = 'manual'
vim.api.nvim_buf_set_lines(0, 0, -1, false, {'a', 'b'})
vim.cmd('1,2fold')
vim.cmd('redraw!')
local folded = vim.fn.screenattr(1, vim.fn.getwininfo(vim.fn.win_getid())[1].textoff + 1)
for _, o in ipairs(out) do
  for r, a in ipairs(o.attrs) do
    for k, x in ipairs(a) do a[k] = x == folded and 'F' or '.' end
    o.attrs[r] = table.concat(a)
  end
end
io.open(OUT, 'w'):write(vim.json.encode(out))
vim.cmd('qa!')
'''

def gen_levels(r, n):
    """foldexpr's value per line: a random walk of levels, a fold start
    now and then"""
    out, lv = [], 0
    for _ in range(n):
        x = r.random()
        if x < .25 and lv < 4: lv += 1; out.append('>%d' % lv)
        elif x < .4 and lv > 0: lv -= 1; out.append('%d' % lv)
        elif x < .5 and lv > 0: out.append('>%d' % lv)
        else: out.append('%d' % lv)
    return out

KEYS = ['C-e', 'C-y', 'C-d', 'C-u', 'C-f', 'C-b', 'zt', 'zz', 'zb', 'H', 'M', 'L', 'gj', 'gk']

def gen_case(r):
    text = gen_text(r)
    # foldtext()'s cases: a C comment's start, fold markers, blank lines
    lines = text.split('\n')
    for i in range(len(lines)):
        x = r.random()
        if x < .05: lines[i] = r.choice(['/*', '// x', '/* a {{{1', '   ', '\t', ' * b', 'c }}} d', '{{{']) + lines[i]
    text = '\n'.join(lines)
    n = max(1, text.count('\n') + (not text.endswith('\n')))
    toks = []
    for _ in range(r.randint(1, 24)):
        x = r.random()
        if x < .2: toks.append(['c', r.randrange(n)])
        elif x < .3: toks.append(['o', r.randrange(n)])
        elif x < .33: toks.append(['zM'])
        elif x < .35: toks.append(['zR'])
        elif x < .5: toks.append(['g', r.randrange(n)])
        else:
            k = r.choice(KEYS)
            # [count]zt puts line count at the top, which keys does not take yet
            toks.append([('' if k[0] == 'z' else r.choice(['', '', '', '2', '3', '7'])) + k])
    return {'text': text, 'levels': gen_levels(r, n), 'rows': r.choice([6, 10, 15, 24]),
            'cols': r.choice([20, 40, 60, 80]), 'wrap': r.random() < .5,
            'foldlevel': r.choice([0, 1, 99, 99]), 'tokens': toks}

def nvim_key(t):
    if t[0] == 'c': return ':%dfoldclose\r' % (t[1] + 1)
    if t[0] == 'o': return ':%dfoldopen\r' % (t[1] + 1)
    if t[0] == 'g': return ':call cursor(%d, 1)\r' % (t[1] + 1)
    s = t[0]
    i = 0
    while s[i].isdigit(): i += 1
    k = s[i:]
    return s[:i] + ('<%s>' % k if k.startswith('C-') else k)

def run_nvim(c):
    d = tempfile.mkdtemp()
    open(d + '/f', 'w').write(c['text'])
    json.dump(dict(c, keys=[nvim_key(t) for t in c['tokens']]), open(d + '/in.json', 'w'))
    open(d + '/t.lua', 'w').write('local IN, OUT = %r, %r\n' % (d + '/in.json', d + '/out.json') + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + d + '/t.lua', 'f'],
                   cwd=d, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    return json.load(open(d + '/out.json'))

def main():
    a = sys.argv[1:]
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap foldlevel, L the levels, F the text in hex, then per
    # screen (the first, then one after each command) T the command (none
    # before the first), W top line cursor line col, R a row in hex, A
    # its cells (F Folded, . not); E ends the case
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d\nL %s\nF %s\n' % (c['rows'], c['cols'], c['wrap'], c['foldlevel'],
                                                    ' '.join(c['levels']), c['text'].encode().hex()))
            for i, s in enumerate(o):
                if i: f.write('T %s\n' % ' '.join(map(str, c['tokens'][i - 1])))
                f.write('W %d %d %d\n' % (s['top'], s['line'], s['col']))
                for row, at in zip(s['rows'], s['attrs']): f.write('R %s\nA %s\n' % (row.encode().hex(), at))
            f.write('E\n')

if __name__ == '__main__':
    main()
