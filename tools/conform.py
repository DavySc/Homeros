#!/usr/bin/env python3
"""Differential test of the Vim keys against Neovim (DESIGN.md, Sources of truth).
usage: conform.py DRIVER N [seed] [--record FILE | --replay FILE]
Generates N random key sequences from the supported set on random text, runs
each in nvim --headless (FEATURES.md settings, swapfile off) and in DRIVER
(tests/conform_drv), and compares the lines and the cursor.  --record also
saves the cases with Neovim's results, so `make test` can replay them without
Neovim (--replay).  Exit 1 on any difference."""
import json, os, random, subprocess, sys, tempfile

WORDS = ['foo', 'bar_1', 'x', '(', ').', ',', 'é', 'λx', '--', '']
INS = list('abc xy,.(') + ['é', '<CR>', '<CR>', '<BS>', '<BS>', '<Tab>', '  ']
NORM = ['h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'gg', 'G', 'x', 'dd', 'u', '<C-r>']
CODES = {'<Esc>': 27, '<CR>': 13, '<BS>': 127, '<Tab>': 9, '<C-r>': 18}

def gen_text(r):
    lines = []
    for _ in range(r.randint(0, 6)):
        ind = r.choice(['', '', '    ', '  ', '\t'])
        lines.append(ind + ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 5))))
    return '\n'.join(lines) + ('\n' if lines and r.random() < .8 else '')

def gen_keys(r):
    keys = []
    for _ in range(r.randint(1, 14)):
        if r.random() < .3:
            keys.append(r.choice('iaIAoO'))
            keys += [r.choice(INS) for _ in range(r.randint(0, 5))]
            keys.append('<Esc>')
        else:
            if r.random() < .25:   # a count; never followed by 0, which would extend it
                keys.append(str(r.randint(1, 4)))
                keys.append(r.choice(NORM[:4] + NORM[5:]))
            else:
                keys.append(r.choice(NORM))
    return keys

LUA = r'''
local cases = vim.json.decode(io.open(IN):read('*a'))
local out = {}
for _, c in ipairs(cases) do
  local old = vim.api.nvim_get_current_buf()
  local b = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_set_current_buf(b)
  vim.api.nvim_buf_delete(old, {force = true})
  vim.bo.undolevels = -1
  local lines = vim.split(c.text, '\n', {plain = true})
  if #lines > 1 and lines[#lines] == '' then table.remove(lines) end
  vim.api.nvim_buf_set_lines(b, 0, -1, false, lines)
  vim.bo.undolevels = 1000
  vim.api.nvim_win_set_cursor(0, {1, 0})
  local ks = table.concat(vim.tbl_map(function(k) return k == '<' and '<lt>' or k end, c.keys))
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(ks, true, false, true), 'xt', false)
  local cur = vim.api.nvim_win_get_cursor(0)
  out[#out + 1] = {lines = vim.api.nvim_buf_get_lines(b, 0, -1, false), cursor = cur}
end
io.open(OUT, 'w'):write(vim.json.encode(out))
vim.cmd('qa!')
'''
SETUP = ('set noswapfile nomore expandtab shiftwidth=4 softtabstop=4 autoindent smarttab '
         'nostartofline backspace=indent,eol,start tabstop=8 shortmess+=I')

def nvim(cases):
    d = tempfile.mkdtemp()
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump(cases, open(fi, 'w'))
    open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + fl],
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    return json.load(open(fo))

def ours(drv, c):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(c['text'])
    keys = ' '.join(str(CODES[k]) if k in CODES else ' '.join(str(ord(ch)) for ch in k) for k in c['keys'])
    p = subprocess.run([drv, f.name], input=keys.encode(), capture_output=True, timeout=20)
    os.unlink(f.name)
    # a split character must show as a difference, not stop the run
    if p.returncode: return {'error': p.returncode, 'stderr': p.stderr[-2000:].decode(errors='backslashreplace')}
    return json.loads(p.stdout.decode(errors='replace'))

def main():
    a = sys.argv[1:]
    drv, mode, path = a[0], None, None
    if '--record' in a: mode, path = 'record', a[a.index('--record') + 1]
    if '--replay' in a: mode, path = 'replay', a[a.index('--replay') + 1]
    seed = None
    if mode == 'replay':
        cases = json.load(open(path))
        want = [c.pop('nvim') for c in cases]
    else:
        n, seed = int(a[1]), int(a[2]) if len(a) > 2 and a[2][0] != '-' else 1
        r = random.Random(seed)
        cases = [{'text': gen_text(r), 'keys': gen_keys(r)} for _ in range(n)]
        want = nvim(cases)
    bad = 0
    for c, w in zip(cases, want):
        g = ours(drv, c)
        if g.get('lines') != w['lines'] or g.get('cursor') != w['cursor']:
            bad += 1
            if bad <= 5:
                print('DIFF text=%s keys=%s\n  nvim  %r %r\n  ours  %r %r' % (
                    json.dumps(c['text'], ensure_ascii=False), json.dumps(c['keys'], ensure_ascii=False), w['lines'], w['cursor'], g.get('lines'), g.get('cursor') or g))
    print('%d sequences%s: %d differ from Neovim' % (len(cases), '' if seed is None else ' (seed %d)' % seed, bad))
    if mode == 'record':
        for c, w in zip(cases, want): c['nvim'] = w
        json.dump(cases, open(path, 'w'), ensure_ascii=False, indent=0)
    sys.exit(1 if bad else 0)

main()
