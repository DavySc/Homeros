#!/usr/bin/env python3
"""Records conceal as Neovim draws it (conceallevel=2, concealcursor=""),
for test_core_conceal.c.  usage: test_core_conceal.py N [SEED] --record FILE
Each case: a random Org text of math notes, a window of random size with
or without wrap, the spans syntax_conceal gives it (from
test_core_conceal --spans, at most 64 a line, so build it first) made
Conceal matches (matchaddpos() with a 'conceal' character, or none:
hidden), the cursor at a random place, and sometimes Visual mode from
another line.  Records each text row's
characters (screenstring()).

Known difference: a concealed double-width character.  The Org syntax
conceals none (measured: markers only, for x^{漢} a_{全} *漢* =全= [[漢][全]]
~漢~ +漢+), so the core does not copy how Neovim draws one: the '<' and '>'
fillers around it, and win_line() drawing a line taller than its height
when one ends a wrapped row (Neovim's bug).  Reduced case of the bug: rows
7, cols 14, wrap, "top\n012345678角全角全\nnext\nlast\n", matchaddpos() span
line 1 bytes 6..12 with "x"."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP

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
local v = vim.fn.winsaveview()
io.open(OUT, 'w'):write(vim.json.encode({rows = rows, view = {v.topline - 1, v.leftcol, v.skipcol}, pos = pos, vp = vp}))
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

# Davy's math notes: org-pretty-entities, sub- and superscripts, emphasis
WORDS = ['\\leq', '\\geq', '\\alpha', '\\to', '\\sqcup', '\\wedge', '\\vee', '\\neg', '\\infty', '\\subseteq',
         '\\top', '\\bot', 'x^{2}', 'a_{i}', 'x^{n+1}', 'y_{0}^{2}', '*bold*', '/it/', '_under_', '=verb=', '~code~',
         '+gone+', 'lattice', 'order', 'a', 'x', '\t', '漢', 'λx', '😀', '(', ')', ',', '--', 'é']

def gen_text(r):
    lines = []
    for _ in range(r.choice([1, 3, 8, 20, 40])):
        k = r.random()
        n = r.choice([0, 1, 3, 6, 12, 30]) if k > .02 else 80   # sometimes more than 64 spans
        ws = [r.choice(WORDS) if k > .02 else '\\alpha' for _ in range(n)]
        lines.append(r.choice(['', '', '* ', '- ', '  ']) + ''.join(w + r.choice([' ', ' ', '', '\t']) for w in ws))
    return '\n'.join(lines) + '\n'

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'san', 'test_core_conceal')

def get_spans(text):
    d = tempfile.mkdtemp()
    open(d + '/f.org', 'w').write(text)
    out = subprocess.run([BIN, '--spans', d + '/f.org'], capture_output=True, text=True, check=True).stdout
    return [[int(k[1]), int(k[2]), int(k[3]), '' if k[4] == '-' else bytes.fromhex(k[4]).decode()]
            for k in (x.split() for x in out.splitlines())]

def gen_case(r):
    text = gen_text(r)
    n = text.count('\n')
    c = {'rows': r.choice([5, 10, 24]), 'cols': r.choice([20, 40, 80]), 'wrap': r.random() < .5,
         'text': text, 'spans': get_spans(text), 'cur': [r.randint(1, n), r.choice([0, 1, 2, 5, 9, 20, 60])]}
    if r.random() < .15: c['vis'] = [r.randint(1, n), r.choice([0, 3, 9])]
    return c

def main():
    a = sys.argv[1:]
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap line col (the cursor), V line col (Visual's start,
    # if any), K line a z s-in-hex (a span), W the view (top line,
    # leftcol, skipcol), F the text in hex, R each text row in hex, E
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d %d\n' % ((c['rows'], c['cols'], c['wrap']) + tuple(o['pos'])))
            if o.get('vp'): f.write('V %d %d\n' % tuple(o['vp']))
            for k in c['spans']: f.write('K %d %d %d %s\n' % (k[0], k[1], k[2], k[3].encode().hex() or '-'))
            f.write('W %d %d %d\n' % tuple(o['view']))
            f.write('F %s\n' % c['text'].encode().hex())
            for row in o['rows']: f.write('R %s\n' % row.encode().hex())
            f.write('E\n')
    print('%d cases (seed %d) recorded' % (n, seed))

if __name__ == '__main__': main()
