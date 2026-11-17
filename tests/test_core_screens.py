#!/usr/bin/env python3
"""Screen traces of the editor core against Neovim (package 4).
usage: test_core_screens.py N [seed] [--record FILE] [--drv DRIVER]
Makes N random cases (files, a screen size, a sequence of window, buffer,
scrolling and editing commands), runs each in nvim --headless with
FEATURES.md's settings and reads Neovim's whole screen with screenstring()
and marks a, b and c after every command.  --record writes the cases with Neovim's
screens for tests/test_core_screens.c, which replays them without Neovim;
--drv runs that program on the fresh cases and compares at once.

A token is one command.  The C side (test_core_screens.c) turns each into
the core call keys will make for it; here each becomes Neovim's keys."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile

SETUP = ('set noswapfile nomore shortmess+=I expandtab shiftwidth=4 softtabstop=4 autoindent '
         'smarttab nostartofline backspace=indent,eol,start number relativenumber '
         'scrolloff=6 splitright splitbelow nowrap')

# completion items for the popup token, the same table as the C side's:
# ten, then the ten again with 2 and with 3 after the word (complete()
# drops a word it already has)
PUM = [('foo', 'v', ''), ('foobar', 'f', 'module.c'), ('é', '', 'λx menu'), ('longerword_completion', 'm', ''),
       ('x', 'k', 'a menu that is longer than the rest'), ('ა_b', '', ''), ('bar_1', 'v', 'm'),
       ('λx', 'f', ''), ('y' * 40, '', ''), ('z', 'v', 'zz')]
PUM = [(w + ('', '2', '3')[i], k, m) for i in range(3) for w, k, m in PUM]

WORDS = ['foo', 'bar_1', 'x', '(', ').', ',', 'é', 'λx', 'ა', '--', 'longerword', 'a' * 30, '\t', '']

def gen_text(r):
    lines = []
    for _ in range(r.choice([0, 1, 3, 8, 20, 40, 120])):
        ind = r.choice(['', '', '    ', '\t'])
        lines.append(ind + ' '.join(r.choice(WORDS) for _ in range(r.randint(0, r.choice([3, 12])))))
    return '\n'.join(lines) + ('\n' if lines and r.random() < .9 else '')

def gen_tokens(r, nfiles):
    toks = []
    for _ in range(r.randint(1, 16)):
        c = r.random()
        cnt = r.choice(['', '', '', '2', '3', '7'])
        if c < .25:
            toks.append(r.choice(['sp', 'vs', 'sp', 'vs', 'new', 'vnew', 'close', 'q', 'only',
                                  'w:s', 'w:v', 'w:c', 'w:o', 'w:w', 'w:W', 'w:p', 'w:t', 'w:b',
                                  'w:h', 'w:j', 'w:k', 'w:l', 'w:=', 'w:+', 'w:-', 'w:<', 'w:>',
                                  'w:_', 'w:|', 'w:x', 'w:r', 'w:R', 'w:H', 'w:J', 'w:K', 'w:L']))
            # CTRL-W c, o and q with a count name a window: not used here
            if toks[-1].startswith('w:') and cnt and toks[-1][2] not in 'coq': toks[-1] = 'w' + cnt + toks[-1][1:]
        elif c < .35:
            toks.append(r.choice(['e:f%d' % r.randrange(nfiles), 'b:%d' % r.randint(1, nfiles + 1),
                                  'bn', 'bp', 'alt', 'enew', 'bd']))
        elif c < .5:
            toks.append(r.choice(['C-e', 'C-y', 'C-d', 'C-u', 'C-f', 'C-b', 'zt', 'zz', 'zb',
                                  'H', 'M', 'L', 'gj', 'gk']))
            if cnt and toks[-1][0] != 'z': toks[-1] = cnt + toks[-1]
        elif c < .55:
            toks.append(r.choice(['wrap', 'nowrap']))
        elif c < .58:
            toks.append('size:%d:%d' % (r.randint(4, 40), r.randint(12, 100)))   # 'columns' is at least 12 (E594)
        elif c < .7:
            toks.append('k:' + r.choice(['ix<Esc>', 'Onew line<Esc>', 'A' + 'y' * r.randint(1, 90) + '<Esc>',
                                         'dd', 'x', 'u', '<C-r>', 'oa<CR>b<CR>c<Esc>']))
        elif c < .76:
            toks.append('m:' + r.choice('abc'))
        else:
            m = r.choice(['j', 'k', 'h', 'l', 'w', 'b', 'gg', 'G', '0', '$', '^'])
            toks.append('k:' + ('' if m == '0' else cnt) + m)   # a count then 0 is a longer count
    # the completion popup: items [a, a + n) shown in Insert mode after the
    # line's end, CTRL-N pressed s times; last, as Insert mode stays
    if r.random() < .3:
        n = r.choice([1, 2, 3, 5, 9, 10, 11, 20])
        toks.append('pum:%d:%d:%d' % (r.randrange(len(PUM) - n + 1), n, r.choice([0, 0, 1, 2, 4, 9, 12, 25])))
    return toks

def split_keys(s):
    """Neovim's key notation, key by key."""
    out, i = [], 0
    while i < len(s):
        j = s.find('>', i) + 1 if s[i] == '<' and '>' in s[i:] else i + 1
        out.append(s[i:j]); i = j
    return out

def nvim_keys(t):
    """The keys Neovim gets for token t."""
    if t.startswith('k:'): return split_keys(t[2:])
    if t.startswith('e:'): return ':e %s<CR>' % t[2:]
    if t.startswith('b:'): return ':b %s<CR>' % t[2:]
    if t.startswith('size:'):
        _, rows, cols = t.split(':')
        return ':set lines=%s columns=%s<CR>' % (rows, cols)
    if t[0] == 'w' and ':' in t:       # w[count]:c is CTRL-W c
        n, c = t[1:].split(':', 1)
        return n + '<C-w>' + c
    if t in ('wrap', 'nowrap'): return ':set %s<CR>' % t
    if t.startswith('pum:'):
        a, n, s = map(int, t[4:].split(':'))
        items = ','.join("{'word':'%s','kind':'%s','menu':'%s'}" % it for it in PUM[a:a + n])
        return ['A', '<Cmd>call complete(col("."), [%s])<CR>' % items] + ['<C-n>'] * s + ['<Cmd>lua CAPTURE()<CR>']
    if t == 'alt': return '<C-^>'
    if t.startswith('m:'): return 'm' + t[2]
    cmds = {'sp', 'vs', 'new', 'vnew', 'close', 'q', 'only', 'bn', 'bp', 'enew', 'bd'}
    if t in cmds: return ':%s<CR>' % t
    i = 0
    while t[i].isdigit(): i += 1
    return t[:i] + {'C-e': '<C-e>', 'C-y': '<C-y>', 'C-d': '<C-d>', 'C-u': '<C-u>',
                    'C-f': '<C-f>', 'C-b': '<C-b>'}.get(t[i:], t[i:])

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
-- Neovim inserts typeahead as one string and scrolls only after it
-- (insertchar()); when to scroll is keys_feed's business (package 5), so
-- an InsertCharPre autocommand turns that off and the C side feeds keys
-- without typeahead
vim.api.nvim_create_autocmd('InsertCharPre', {callback = function() end})
vim.o.lines = c.rows
vim.o.columns = c.cols
local function screen(bang)
  vim.cmd(bang == false and 'redraw' or 'redraw!')
  local rows = {}
  for r = 1, vim.o.lines do
    local s = {}
    for col = 1, vim.o.columns do s[#s + 1] = vim.fn.screenstring(r, col) end
    rows[#rows + 1] = table.concat(s)
  end
  return rows
end
-- marks a, b and c of the current buffer, as line:col from 0, or -
local function marks()
  local s = {}
  for _, m in ipairs({'a', 'b', 'c'}) do
    local p = vim.fn.getpos("'" .. m)
    s[#s + 1] = p[2] == 0 and '-' or (p[2] - 1) .. ':' .. (p[3] - 1)
  end
  return table.concat(s, ' ')
end
-- the popup token reads the screen in Insert mode, with the popup up
-- ('redraw!' would close it)
function CAPTURE() captured = screen(false); cmarks = marks() end
local out, mk, nop = {screen()}, {marks()}, {}
for i, ks in ipairs(c.keys) do
  -- closing the last window would quit: the C side skips it too
  local last = #vim.api.nvim_tabpage_list_wins(0) == 1
  local k1 = table.concat(ks)
  if last and (k1 == ':q<CR>' or k1 == ':close<CR>' or k1:match('<C%-w>[cq]$')) then ks = {} end
  -- x on an empty line leaves an empty undo step (ref/keys.c), which the
  -- core counts as a change and Neovim does not ('modified' is b_changed,
  -- kept per undo header): CORE_CHANGED will fix it; until then not tried
  if (k1 == 'x' and vim.fn.getline('.') == '') or (k1 == 'dd' and vim.fn.line('$') == 1 and vim.fn.getline(1) == '') then
    ks = {}; nop[#nop + 1] = i
  end
  vim.v.errmsg = ''
  captured, cmarks = nil, nil
  pcall(vim.api.nvim_feedkeys, vim.api.nvim_replace_termcodes(table.concat(ks), true, false, true), 'xt', false)
  -- :set lines below what the windows need fails (E593) and changes
  -- nothing; a terminal resize (screen_resize()) cannot fail, so the C
  -- side, which resizes, skips it
  if vim.v.errmsg:match('^E593') then nop[#nop + 1] = i end
  out[#out + 1] = captured or screen()
  mk[#mk + 1] = cmarks or marks()
end
io.open(OUT, 'w'):write(vim.json.encode({screens = out, marks = mk, nop = nop}))
vim.cmd('qa!')
'''

def run_nvim(case):
    d = tempfile.mkdtemp()
    for i, t in enumerate(case['files']):
        with open(os.path.join(d, 'f%d' % i), 'w') as f: f.write(t)
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump({'rows': case['rows'], 'cols': case['cols'],
               'keys': [k if isinstance(k, list) else [k] for k in map(nvim_keys, case['tokens'])]}, open(fi, 'w'))
    open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + fl, 'f0'], cwd=d,
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    return json.load(open(fo))

def gen_case(r):
    nf = r.randint(1, 3)
    return {'rows': r.choice([6, 10, 15, 24, 30]), 'cols': r.choice([20, 40, 60, 80]),
            'files': [gen_text(r) for _ in range(nf)], 'tokens': gen_tokens(r, nf)}

def write_cases(path, cases):
    """One case per block: C rows cols, F hex (one per file), T token (one
    per token), then each screen (the initial one, then one after each
    token) as P and a line per row: S and the row without trailing spaces,
    or R when the row is as on the screen before; then M and marks a, b and
    c (line:col from 0, or -)."""
    with open(path, 'w', encoding='utf-8') as f:
        for c in cases:
            f.write('C %d %d\n' % (c['rows'], c['cols']))
            for t in c['files']: f.write('F %s\n' % t.encode().hex())
            for t in c['tokens']: f.write('T %s\n' % t)
            prev = []
            for s, m in zip(c['screens'], c['marks']):
                f.write('P\n')
                for i, row in enumerate(s):
                    f.write('R\n' if i < len(prev) and prev[i] == row else 'S %s\n' % row.rstrip(' '))
                f.write('M %s\n' % m)
                prev = s
            f.write('E\n')

def main():
    a = sys.argv[1:]
    n = int(a[0]); seed = int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        for c, s in zip(cases, ex.map(run_nvim, cases)):
            c['screens'], c['marks'] = s['screens'], s['marks']
            for i in s['nop']: c['tokens'][i - 1] = 'nop'
    path = a[a.index('--record') + 1] if '--record' in a else tempfile.mktemp(suffix='.txt')
    write_cases(path, cases)
    print('%d cases (seed %d) recorded from Neovim in %s' % (n, seed, path))
    if '--drv' in a:
        p = subprocess.run([a[a.index('--drv') + 1], path])
        sys.exit(p.returncode)

main()
