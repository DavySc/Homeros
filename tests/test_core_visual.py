#!/usr/bin/env python3
"""Records the Visual area as Neovim draws it, for test_core_visual.c.
usage: test_core_visual.py N [SEED] --record FILE
Each case: a random text, a window of random size with or without wrap,
VIsual and the cursor at random places (the cursor on the line's end after
$ in a fifth), Visual mode v, V or CTRL-V.  The cells whose screenattr() changes when
Visual is cleared are the area."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP, gen_text

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
vim.o.lines = c.rows
vim.o.columns = c.cols
vim.o.wrap = c.wrap
-- line p[1], character p[2] (or the line's end), as a cursor can be
local function at(p)
  local l = math.min(p[1], vim.fn.line('$'))
  vim.fn.setcursorcharpos(l, math.min(p[2], vim.fn.strchars(vim.fn.getline(l), 1)) + 1)
end
-- where a start puts the cursor (see test_core_screens.py)
vim.cmd('normal! 1G^')
at(c.v)
vim.cmd('normal! ' .. vim.api.nvim_replace_termcodes(c.mode, true, false, true))
at(c.cur)
if c.eol then vim.cmd('normal! $') end
vim.cmd('redraw!')
local info = vim.fn.getwininfo(vim.fn.win_getid())[1]
-- the cells whose attribute changes when Visual is cleared
local function attrs()
  local a = {}
  for r = 1, info.height do
    for col = info.textoff + 1, info.width do a[#a + 1] = vim.fn.screenattr(r, col) end
  end
  return a
end
local on = attrs()
vim.cmd('hi Visual NONE | redraw!')
local off, rows, i = attrs(), {}, 0
for r = 1, info.height do
  local s = {}
  for col = info.textoff + 1, info.width do i = i + 1; s[#s + 1] = on[i] ~= off[i] and 'V' or '.' end
  rows[#rows + 1] = (table.concat(s):gsub('%.+$', ''))
end
local v = vim.fn.winsaveview()
io.open(OUT, 'w'):write(vim.json.encode({rows = rows, view = {v.topline - 1, v.leftcol, v.skipcol}, pos = {vim.fn.line('v') - 1, vim.fn.col('v') - 1,
  vim.fn.line('.') - 1, vim.fn.col('.') - 1, vim.fn.getcurpos()[5] == vim.v.maxcol and 1 or 0}}))
vim.cmd('qa!')
'''

def run_nvim(c):
    d = tempfile.mkdtemp()
    open(d + '/f', 'w').write(c['text'])
    json.dump(c, open(d + '/in.json', 'w'))
    open(d + '/t.lua', 'w').write('local IN, OUT = %r, %r\n' % (d + '/in.json', d + '/out.json') + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + d + '/t.lua', 'f'],
                   cwd=d, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    return json.load(open(d + '/out.json'))

def gen_case(r):
    text = ''.join(gen_text(r).splitlines(True)[:25])   # keeps the file small
    n = text.count('\n') + 1
    pos = lambda: [r.randint(1, n), r.choice([0, 1, 2, 5, 9, 20, 60])]
    return {'rows': r.choice([5, 10, 24]), 'cols': r.choice([20, 40, 80]), 'wrap': r.random() < .5,
            'text': text, 'mode': r.choice(['v', 'V', '<C-v>']), 'v': pos(), 'cur': pos(), 'eol': r.random() < .2}

def main():
    a = sys.argv[1:]
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap mode vline vcol line col eol, W the view (top line,
    # leftcol, skipcol), F the text in hex, A
    # each text row's Visual cells (V) up to the last, E
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d %d %d %d %d %d\n' % ((c['rows'], c['cols'], c['wrap'], {'v': 118, 'V': 86}.get(c['mode'], 22)) + tuple(o['pos'])))
            f.write('W %d %d %d\n' % tuple(o['view']))
            f.write('F %s\n' % c['text'].encode().hex())
            for row in o['rows']: f.write('A %s\n' % row)
            f.write('E\n')
    print('%d cases (seed %d) recorded' % (n, seed))

if __name__ == '__main__': main()
