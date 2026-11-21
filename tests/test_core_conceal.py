#!/usr/bin/env python3
"""Records conceal as Neovim draws it (conceallevel=2, concealcursor=""),
for test_core_conceal.c.  usage: test_core_conceal.py N [SEED] --record FILE
Each case: a random text, a window of random size with or without wrap,
random spans on random lines made Conceal matches (matchaddpos() with a
'conceal' character, or none: hidden), the cursor at a random place, and
sometimes Visual mode from another line.  Records each text row's
characters (screenstring())."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP, gen_text

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
vim.o.lines = c.rows
vim.o.columns = c.cols
vim.o.wrap = c.wrap
vim.o.conceallevel = 2
vim.o.concealcursor = ''
for _, k in ipairs(c.spans) do
  vim.fn.matchaddpos('Conceal', {{k[1] + 1, k[2] + 1, k[3] - k[2]}}, 10, -1, {conceal = k[4]})
end
local function go(p)
  local l = math.min(p[1], vim.fn.line('$'))
  vim.fn.setcursorcharpos(l, math.min(p[2], vim.fn.strchars(vim.fn.getline(l), 1)) + 1)
end
if c.vis then go(c.vis) vim.cmd('normal! v') end
go(c.cur)
if c.left then vim.fn.winrestview({leftcol = c.left}) end
local pos = {vim.fn.line('.') - 1, vim.fn.col('.') - 1}
local vp = c.vis and {vim.fn.line('v') - 1, vim.fn.col('v') - 1} or nil
vim.cmd('redraw!')
local info = vim.fn.getwininfo(vim.fn.win_getid())[1]
local rows = {}
for r = 1, info.height do
  local s = {}
  for col = info.textoff + 1, info.width do s[#s + 1] = vim.fn.screenstring(r, col) end
  rows[#rows + 1] = table.concat(s)
end
local function filler()
  for r = 1, info.height do if vim.fn.screenstring(r, 1) == '~' then return r end end
  return 0
end
local v, bot, fl = vim.fn.winsaveview(), vim.fn.line('w$'), filler()
-- win_line() can draw a line taller than its height when a concealed
-- double-width character wraps (Neovim's bug): the view or the last line
-- shown or the first '~' row then differ from those without conceal
vim.o.conceallevel = 0
vim.cmd('redraw!')
local v0 = vim.fn.winsaveview()
local bug = bot ~= vim.fn.line('w$') or fl ~= filler() or v.topline ~= v0.topline or v.skipcol ~= v0.skipcol
io.open(OUT, 'w'):write(vim.json.encode({rows = rows, view = {v.topline - 1, v.leftcol, v.skipcol}, pos = pos, vp = vp, bug = bug}))
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

def gen_spans(r, lines):
    # line-relative byte spans on character boundaries, in byte order
    spans = []
    for l, s in enumerate(lines):
        if not s or r.random() < .5: continue
        b, i = s.encode(), 0
        while i < len(s):
            i += r.randint(0, 6)
            if i >= len(s): break
            j = min(len(s), i + r.choice([1, 1, 2, 3, 5]))
            a, z = len(s[:i].encode()), len(s[:j].encode())
            spans.append([l, a, z, r.choice(['', '', '≤', 'x', '²', '→', '∧'])])
            i = j + r.randint(0, 3)
    return spans

def gen_case(r):
    text = ''.join(gen_text(r).splitlines(True)[:25])
    lines = text.split('\n')
    n = len(lines)
    c = {'rows': r.choice([5, 10, 24]), 'cols': r.choice([20, 40, 80]), 'wrap': r.random() < .5,
         'text': text, 'spans': gen_spans(r, lines), 'cur': [r.randint(1, n), r.choice([0, 1, 2, 5, 9, 20, 60])]}
    if r.random() < .15: c['vis'] = [r.randint(1, n), r.choice([0, 3, 9])]
    return c

def main():
    a = sys.argv[1:]
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap line col (the cursor), X if Neovim drew a line
    # taller than its height (not compared), V line col (Visual's start,
    # if any), K line a z s-in-hex (a span), W the view (top line,
    # leftcol, skipcol), F the text in hex, R each text row in hex, E
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d %d\n' % ((c['rows'], c['cols'], c['wrap']) + tuple(o['pos'])))
            if o['bug']: f.write('X\n')
            if o.get('vp'): f.write('V %d %d\n' % tuple(o['vp']))
            for k in c['spans']: f.write('K %d %d %d %s\n' % (k[0], k[1], k[2], k[3].encode().hex() or '-'))
            f.write('W %d %d %d\n' % tuple(o['view']))
            f.write('F %s\n' % c['text'].encode().hex())
            for row in o['rows']: f.write('R %s\n' % row.encode().hex())
            f.write('E\n')
    print('%d cases (seed %d) recorded' % (n, seed))

if __name__ == '__main__': main()
