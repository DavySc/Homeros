#!/usr/bin/env python3
"""Differential test of src/snip against Neovim 0.12's vim.snippet.
usage: snip.py DRIVER N [seed] [--record FILE] [--catalogue snippets.md]
DRIVER is build/san/test_snip (make test TESTS=snip builds it).  Each case
is a body (from --catalogue, or made up: stops, mirrors, empty stops, $0,
line breaks, Tabs, multibyte), the text before the cursor, and keys; Neovim
expands the body with vim.snippet.expand() at the end of that text in
Insert mode (nvim --headless, keys sent one at a time through nvim_input
so its autocmds run as when typing), the driver reaches it through the
popup.  The lines, the cursor and the mode must match ('s' and 'v' are
both 'v'; Neovim's select mode is ours Visual).  --record writes the
cases that match to FILE (test_snip replays it).  Exit 1 on a difference.

Known differences, where Neovim's result is a quirk of its extmarks rather
than a rule (measured; the generator avoids the first two):
- a mirror next to a stop of its own number, with only empty stops between,
  never updates (their extmarks share an edge);
- Enter in a stop that has mirrors copies part of the new line into them
  and loses text (set_text over a range that spans lines);
- a Delete or Backspace that leaves two stops of one number touching
  (1 case in 300 of seed 14) goes the same way;
- Neovim refuses a placeholder holding text and a stop, and a '$' that
  starts nothing; VS Code, whose grammar Davy's catalogue uses, takes them
  (those cases are 'refused' here and not compared).
- CTRL-W or CTRL-U after the expansion and an arrow key stops at the
  insert's start one character later in Neovim, whose expansion syncs undo
  (ins_need_undo then keeps Insstart_orig moving): 1 case in 196 of seed 4.
CTRL-W in Normal mode waits for a window command: both sides skip it."""
import json, os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

KEYS = list('ax é') + ['<BS>', '<BS>', '<Del>', '<Tab>', '<Tab>', '<Tab>', '<S-Tab>', '<S-Tab>', '<Esc>', '<CR>', '<C-w>', '<Left>', '<Right>']
PRE = ['', '    ', 'x ', '    if a = ', '\tb.']
TEXT = ['a', ' ', '\n', '(', ', ', '_type', 'é', '\t', '@', '.', 'λ']

def gen_body(r):
    n = r.randint(1, 4)
    ph = {i: r.choice(['', 'x', 'col', 'é_y', 'a b']) for i in range(1, n + 1)}
    seen, out, run = set(), [], set()
    zero = r.random() < .5
    for _ in range(r.randint(1, 7)):
        if r.random() < .5:
            out.append(r.choice(TEXT)); run = set()
            continue
        i = r.randint(1, n)
        # Neovim's mirror touching its own stop, with only empty stops
        # between, never updates (their extmarks share an edge): a known
        # difference, so never generated
        if i in run: out.append(r.choice(TEXT)); run = set()
        run.add(i)
        if zero and r.random() < .15:
            out.append('$0'); zero = False; continue
        if ph[i] and (i not in seen or r.random() < .5): out.append('${%d:%s}' % (i, ph[i]))
        else: out.append(r.choice(['$%d', '${%d}']) % i)
        seen.add(i)
    return ''.join(out)

def catalogue(path):
    md = open(path).read()
    return [b for b in re.findall(r'```sql\n--[^\n]*\n(.*?)\n```', md, re.S)]

def esc(s): return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')

LUA = r'''
local cases = vim.json.decode(io.open(IN):read('a'))
local out, ci = {}, 0
local function step(f) return function() local ok, e = pcall(f) if not ok then out[#out + 1] = 'error ' .. tostring(e) vim.defer_fn(_G.nextcase, 1) end end end
local function keys(c, i)
  if i > #c.keys then
    local m = vim.api.nvim_get_mode().mode:sub(1, 1)
    local lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)
    local cur = vim.api.nvim_win_get_cursor(0)
    out[#out + 1] = string.format('%s %d %d %s', (m == 's' or m == 'v') and 'v' or m, cur[1], cur[2], table.concat(lines, '\n'))
    return vim.defer_fn(_G.nextcase, 1)
  end
  -- a CTRL-W in Normal mode waits for a window command: both sides skip it
  if not (c.keys[i] == '<C-w>' and vim.fn.mode() == 'n') then vim.api.nvim_input(c.keys[i]) end
  vim.defer_fn(step(function() keys(c, i + 1) end), 20)
end
function _G.nextcase()
  pcall(vim.snippet.stop)
  vim.cmd('stopinsert')
  ci = ci + 1
  if ci > #cases then io.open(OUT, 'w'):write(vim.json.encode(out)) vim.cmd('qa!') return end
  local c = cases[ci]
  vim.cmd('enew!')
  vim.api.nvim_buf_set_lines(0, 0, -1, false, {c.pre})
  vim.defer_fn(step(function()
    vim.api.nvim_input('A')   -- typed, as Davy enters Insert mode (startinsert! would set arrow_used)
    vim.defer_fn(step(function()
      if not pcall(vim.snippet.expand, c.body) then out[#out + 1] = 'refused' return vim.defer_fn(_G.nextcase, 1) end
      vim.defer_fn(step(function() keys(c, 1) end), 20)
    end), 20)
  end), 20)
end
vim.defer_fn(_G.nextcase, 10)
vim.defer_fn(function() io.open(OUT, 'w'):write(vim.json.encode(out)) vim.cmd('qa!') end, 1000 * #cases)
'''
SETUP = ('set noswapfile nomore expandtab shiftwidth=4 softtabstop=4 autoindent smarttab '
         'nostartofline backspace=indent,eol,start tabstop=8 nowrap shortmess+=I scrolloff=6')

def nvim1(cases):
    with tempfile.TemporaryDirectory() as d:
        fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
        json.dump(cases, open(fi, 'w'))
        open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
        try: subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + fl],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        except subprocess.TimeoutExpired: return ['hang']
        return json.load(open(fo))

def main():
    a = sys.argv[1:]
    rec = a[a.index('--record') + 1] if '--record' in a else None
    cat = catalogue(a[a.index('--catalogue') + 1]) if '--catalogue' in a else []
    pos = [x for i, x in enumerate(a) if not x.startswith('--') and (i == 0 or a[i - 1] not in ('--record', '--catalogue'))]
    drv, n, seed = pos[0], int(pos[1]), int(pos[2]) if len(pos) > 2 else 1
    r = random.Random(seed)
    cases = []
    for _ in range(n):
        b = r.choice(cat) if cat and r.random() < .5 else gen_body(r)
        # Enter in a stop with mirrors: Neovim copies part of the new line
        # into them and loses text (set_text on a multi-line range), a known
        # difference, so not generated
        ix = re.findall(r'\$\{?(\d+)', b)
        ks = [k for k in KEYS if k != '<CR>' or len(ix) == len(set(ix))]
        cases.append({'body': b, 'pre': r.choice(PRE), 'keys': [r.choice(ks) for _ in range(r.randint(0, 8))]})
    # a fresh Neovim per case, as the driver starts a fresh editor: a
    # command left half typed (<C-w>) would eat the next case's keys
    with ThreadPoolExecutor(os.cpu_count()) as x:
        nv = [w for part in x.map(nvim1, [[c] for c in cases]) for w in part]
    for i in range(n):
        if nv[i] == 'hang': print('Neovim hung on case %d: %r' % (i, cases[i]))
    keep = [i for i in range(n) if nv[i] not in ('refused', 'hang')]
    with tempfile.NamedTemporaryFile('w', delete=False) as f:
        for i in keep: f.write('%s\n%s\n%s\n' % (esc(cases[i]['body']), esc(cases[i]['pre']), ' '.join(t if t != ' ' else '<Space>' for t in cases[i]['keys'])))
    p = subprocess.run([drv, f.name], capture_output=True, timeout=600)
    os.unlink(f.name)
    if p.returncode: sys.exit('driver failed: %s' % p.stderr.decode(errors='replace')[-2000:])
    ours = p.stdout.decode(errors='surrogateescape').split('\n')
    bad, ok = [], []
    for j, i in enumerate(keep):
        w = nv[i].split(' ', 3)
        want = '%s %s %s %s' % (w[0], w[1], w[2], esc(w[3]))
        (ok if ours[j] == want else bad).append((i, want, ours[j]))
    for i, w, g in bad[:10]:
        print('case %d: body %r pre %r keys %s\n  nvim %s\n  ours %s' % (i, cases[i]['body'], cases[i]['pre'], ' '.join(cases[i]['keys']), w, g))
    print('snip.py: %d of %d cases differ (%d refused by Neovim)' % (len(bad), len(keep), n - len(keep)))
    if rec:
        with open(rec, 'w') as f:
            for i, w, g in ok: f.write('%s\n%s\n%s\n%s\n' % (esc(cases[i]['body']), esc(cases[i]['pre']), ' '.join(t if t != ' ' else '<Space>' for t in cases[i]['keys']), w))
    sys.exit(1 if bad else 0)

main()
