#!/usr/bin/env python3
"""Records 'hlsearch' as Neovim draws it, for test_core_search.c.
usage: test_core_search.py N [SEED] --record FILE
Each case: a random text, a window of random size with or without wrap,
a search typed with / for a piece of the text (sometimes anchored with ^
or $, or across a line break with \\n), then the cursor at a random place.
The cells whose screenattr() changes under 'nohlsearch' are the matches;
of those, the ones that change when CurSearch is redefined are CurSearch."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP, gen_text

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
vim.o.lines = c.rows
vim.o.columns = c.cols
vim.o.wrap = c.wrap
vim.o.hlsearch = true
vim.cmd('normal! 1G^')
pcall(vim.cmd, 'normal! /' .. c.pat .. '\r')
local l = math.min(c.cur[1], vim.fn.line('$'))
vim.fn.setcursorcharpos(l, math.min(c.cur[2], vim.fn.strchars(vim.fn.getline(l), 1)) + 1)
local pos = {vim.fn.line('.') - 1, vim.fn.col('.') - 1}
vim.cmd('redraw!')
local info = vim.fn.getwininfo(vim.fn.win_getid())[1]
local function attrs()
  local a = {}
  for r = 1, info.height do
    for col = info.textoff + 1, info.width do a[#a + 1] = vim.fn.screenattr(r, col) end
  end
  return a
end
local on = attrs()
vim.cmd('hi CurSearch gui=underline cterm=underline guibg=#123456 ctermbg=1 | redraw!')
local cur = attrs()
vim.cmd('set nohlsearch | redraw!')
local off, rows, i = attrs(), {}, 0
for r = 1, info.height do
  local s = {}
  for col = info.textoff + 1, info.width do
    i = i + 1
    s[#s + 1] = on[i] == off[i] and '.' or on[i] ~= cur[i] and 'C' or 'S'
  end
  rows[#rows + 1] = (table.concat(s):gsub('%.+$', ''))
end
local v = vim.fn.winsaveview()
io.open(OUT, 'w'):write(vim.json.encode({rows = rows, view = {v.topline - 1, v.leftcol, v.skipcol}, pos = pos}))
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

def gen_pat(r, lines):
    s = r.choice([x for x in lines if x] or ['a'])
    a = r.randrange(len(s))
    piece = s[a:a + r.randint(1, 3)]
    # typed: a control character is a command on the command line
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in piece): piece = 'a'
    esc = ''.join('\\' + ch if ch in '\\^$.*[]~/' else ch for ch in piece)
    k = r.random()
    if k < .15: return '^' + esc
    if k < .3: return esc + '$'
    if k < .4: return esc + '\\n' if a + len(piece) == len(s) else esc
    if k < .45: return '$'
    if k < .5: return '^'
    return esc

def gen_case(r):
    text = ''.join(gen_text(r).splitlines(True)[:25])
    lines = text.split('\n')
    n = len(lines)
    return {'rows': r.choice([5, 10, 24]), 'cols': r.choice([20, 40, 80]), 'wrap': r.random() < .5,
            'text': text, 'pat': gen_pat(r, lines), 'cur': [r.randint(1, n), r.choice([0, 1, 2, 5, 9, 20, 60])]}

def main():
    a = sys.argv[1:]
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap line col (the cursor), P the pattern in hex, W the
    # view (top line, leftcol, skipcol), F the text in hex, A each text
    # row's cells up to the last match (S Search, C CurSearch), E
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d %d\n' % ((c['rows'], c['cols'], c['wrap']) + tuple(o['pos'])))
            f.write('P %s\n' % c['pat'].encode().hex())
            f.write('W %d %d %d\n' % tuple(o['view']))
            f.write('F %s\n' % c['text'].encode().hex())
            for row in o['rows']: f.write('A %s\n' % row)
            f.write('E\n')
    print('%d cases (seed %d) recorded' % (n, seed))

if __name__ == '__main__': main()
