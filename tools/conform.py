#!/usr/bin/env python3
"""Differential test of the Vim keys against Neovim (DESIGN.md, Sources of truth).
usage: conform.py DRIVER N [seed] [--record FILE | --replay FILE]
Generates N random key sequences from the supported set on random text, runs
each in nvim --headless (FEATURES.md settings, swapfile off) and in DRIVER
(tests/conform_drv), and compares the lines and the cursor.  --record also
saves the cases with Neovim's results, so `make test` can replay them without
Neovim (--replay).  Exit 1 on any difference.

tests/conform-known.json lists known differences: each entry has a "why",
a "keys" regex (and optionally a "text" regex) matched against a case's
keys as typed (''.join) and its text, and an "example" case.  A live case
that differs and matches an entry counts as known, not as a failure; every
example must still differ from Neovim and match its own entry, so a fixed
difference fails the run until its entry is removed.  --record keeps only
the cases that match Neovim; --replay takes no known differences."""
import json, os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

# 2-, 3- and 4-byte characters: Neovim's utf_class() calls é, λ, ა and 𐐀
# word characters and ∀ and 𝓝 (Lean) punctuation.  No comment leaders at
# the start of a line ('comments' changes J with 'formatoptions' j).
WORDS = ['foo', 'bar_1', 'x', '(', ').', ',', 'é', 'λx', 'ა', '𐐀x', '--', '', '∀', 'x𝓝', '0x1f', '-3', '7',
         '"a b"', "'c'", '{', '}', '[y]', '<z>', 'Foo.', 'ẞß',
         'x\u0300\u0301', 'e\u0301a', '\U0001F1F3\U0001F1F1', '\U0001F469\u200D\U0001F4BB', '\u0301']
# the last five are grapheme clusters: combining marks, a flag, a ZWJ
# sequence, and a mark with nothing to combine with
# the characters f, t, r and the text objects look for
CHARS = list('ox(). ,"\'{}[]-é') + ['λ', '∀', '𝓝']
INS = list('abc xy,.(0^') + ['é', 'ა', '𐐀', '∀', '<CR>', '<CR>', '<BS>', '<BS>', '<Tab>', '  ',
      '<Del>', '<C-w>', '<C-u>', '<C-t>', '<C-d>', '<Left>', '<Right>', '<Up>', '<Down>']
REGS = ['a', 'b', 'A', '"', '0', '1', '2', '-', '_']
MOTIONS = ['h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'e', 'W', 'B', 'E', 'ge', 'gE', 'gg', 'go', 'G', '|',
           '_', '+', '-', '<CR>', '<BS>', ' ', 'gj', 'gk', 'g_', '(', ')', '{', '}', '[[', ']]', '[]', '][',
           '[(', '[{', '])', ']}', ';', ',', '%']
FIND = ['f', 'F', 't', 'T']
OBJECTS = [a + o for a in 'ia' for o in 'wWsp([{<b)B"\'`']
OPS = ['d', 'c', 'y', '<', '>', 'g~', 'gu', 'gU']
SIMPLE = ['x', 'X', 'D', 'C', 's', 'S', 'Y', 'J', 'gJ', '~', 'p', 'P', 'gp', 'gP', 'u', '<C-r>', '.', '.',
          '<C-a>', '<C-x>', 'dd', 'cc', 'yy', '>>', '<<', 'g~~', 'guu', 'gUU', 'gv', '<Del>']
VOPS = ['d', 'c', 'y', '<', '>', '~', 'u', 'U', 'J', 'gJ', 'I', 'A', 'p', 'P', 'x', 'X', 'D', 'C', 'S', 'Y',
        'R', 's', '<C-a>', '<C-x>', 'g<C-a>', 'o', 'O', '$', 'gv', '<Esc>', 'r']
NORM = MOTIONS + SIMPLE   # the keys whose count is generated
CODES = {'<Esc>': 27, '<CR>': 13, '<BS>': 127, '<Tab>': 9, '<C-r>': 18, '<C-w>': 23, '<C-u>': 21,
         '<C-t>': 20, '<C-d>': 4, '<C-o>': 15, '<C-a>': 1, '<C-x>': 24, '<C-v>': 22, '<C-p>': 16,
         '<Up>': 0x110000, '<Down>': 0x110001, '<Left>': 0x110002, '<Right>': 0x110003, '<Del>': 0x110009}

def gen_text(r):
    lines = []
    for _ in range(r.randint(0, 6)):
        ind = r.choice(['', '', '    ', '  ', '\t'])
        lines.append(ind + ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 5))))
    return '\n'.join(lines) + ('\n' if lines and r.random() < .8 else '')

def count(r, keys):
    x = r.random()
    if x < .25:   # never followed by 0, which would extend it
        keys.append(str(r.randint(1, 4)))
    elif x < .27:   # two digits, typed one by one (fuzz_keys keeps counts to one)
        keys += list(str(r.randint(10, 19)))

def motion(r, keys, objects=True):
    # text objects only after an operator or in Visual mode: in Normal
    # mode "i(" is an insert left open
    x = r.random()
    if x < .12: keys += [r.choice(FIND), r.choice(CHARS)]
    elif objects and x < .27: keys.append(r.choice(OBJECTS))
    else: keys.append(r.choice(MOTIONS))

def insert(r, keys, cmd=True, sure=True):
    if cmd:
        keys.append(r.choice(['i', 'a', 'I', 'A', 'o', 'O', 'gI', 'R', 's', 'S', 'C']))
        # 3S and 3C fail past the last line
        if keys[-1] in ('S', 'C') and len(keys) > 1 and keys[-2].isdigit(): sure = False
    for _ in range(r.randint(0, 5)):
        x = r.random()
        if x < .06: keys += ['<C-r>', r.choice(['a', '"', '0', '-', '.'])]
        # CTRL-O only where the insert surely started: after a failed
        # motion its keys reach Normal mode, where CTRL-O is the jump list,
        # not built yet (src/keys/README)
        elif x < .1 and sure: keys += ['<C-o>', r.choice(['l', 'h', '$', '0', 'w', 'x', 'dd', 'p', 'u'])]
        else: keys.append(r.choice(INS))
    keys.append('<Esc>')

def command(r, keys, depth=0):
    x = r.random()
    if x < .22:
        count(r, keys)
        insert(r, keys)
    elif x < .45:
        count(r, keys)
        motion(r, keys, False)
    elif x < .65:
        if r.random() < .2: keys += ['"', r.choice(REGS)]
        count(r, keys)
        op = r.choice(OPS)
        keys.append(op)
        count(r, keys)
        if r.random() < .1: keys.append(r.choice(['v', 'V']))
        motion(r, keys)
        if op == 'c': insert(r, keys, False, False)
    elif x < .8:
        if r.random() < .15: keys += ['"', r.choice(REGS)]
        count(r, keys)
        k = r.choice(SIMPLE)
        keys.append(k)
        if k in ('C', 's', 'S', 'cc'): insert(r, keys, False, len(keys) < 2 or not keys[-2].isdigit())
    elif x < .9:
        keys.append(r.choice(['v', 'V', '<C-v>']))
        for _ in range(r.randint(0, 3)):
            count(r, keys)
            motion(r, keys)
        if r.random() < .15: keys += ['"', r.choice(REGS)]
        count(r, keys)
        op = r.choice(VOPS)
        keys.append(op)
        if op == 'r': keys.append(r.choice(CHARS + ['<CR>']))
        if op in ('c', 'I', 'A', 's', 'S', 'C', 'R'): insert(r, keys, False)
        # these keep Visual mode, where @ and Q need :normal (not built)
        if op in ('o', 'O', '$', 'gv'): keys.append('<Esc>')
    elif x < .94:
        count(r, keys)
        keys += ['r', r.choice(CHARS + ['<CR>', '<Tab>'])]
    elif depth == 0 and x < .98:
        # a macro: recorded into q or w (appended with Q or W), never
        # running a register while recording, so nothing recurses
        reg = r.choice('qwQW')
        keys += ['q', reg]
        for _ in range(r.randint(1, 3)): command(r, keys, 1)
        keys.append('q')
    elif depth == 0:
        # not from the Visual mode gv left: Visual @ and Q need :normal
        if keys and keys[-1] == 'gv': keys.append('<Esc>')
        count(r, keys)
        keys += r.choice([['@', 'q'], ['@', 'w'], ['@', '@'], ['Q']])
    else:
        count(r, keys)
        motion(r, keys, False)

def gen_keys(r):
    keys = []
    for _ in range(r.randint(1, 14)):
        command(r, keys)
    return keys

LUA = r'''
local cases = vim.json.decode(io.open(IN):read('*a'))
local out = {}
-- each case opens its text as a file, as the editor does (an empty file is
-- then ML_EMPTY, which nvim_buf_set_lines would not give)
-- (a new file each time: rewriting one would raise W11)
for i, c in ipairs(cases) do
  local F = OUT .. '.' .. i
  local f = io.open(F, 'wb')
  f:write(c.text)
  f:close()
  local old = vim.api.nvim_get_current_buf()
  vim.cmd('silent edit! ' .. vim.fn.fnameescape(F))
  os.remove(F)
  local b = vim.api.nvim_get_current_buf()
  -- an empty unnamed buffer is reused by :edit
  if old ~= b then vim.api.nvim_buf_delete(old, {force = true}) end
  vim.api.nvim_win_set_cursor(0, {1, 0})   -- :edit puts it on the first non-blank
  -- go is gg (Davy, FEATURES.md): sent as gg, since after a beep Neovim
  -- drops the next mapped key, which a map would make go
  local ks = table.concat(vim.tbl_map(function(k) return k == '<' and '<lt>' or k == 'go' and 'gg' or k end, c.keys))
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(ks, true, true, true), 'xt', false)
  local cur = vim.api.nvim_win_get_cursor(0)
  out[#out + 1] = {lines = vim.api.nvim_buf_get_lines(b, 0, -1, false), cursor = cur}
end
io.open(OUT, 'w'):write(vim.json.encode(out))
vim.cmd('qa!')
'''
SETUP = ('set noswapfile nomore expandtab shiftwidth=4 softtabstop=4 autoindent smarttab '
         'nostartofline backspace=indent,eol,start tabstop=8 virtualedit=block nowrap shortmess+=I'
         ' scrolloff=6')   # FEATURES.md's scrolloff, which the core keeps

def nvim1(cases):
    d = tempfile.mkdtemp()
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump(cases, open(fi, 'w'))
    open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
    # % is Neovim's built-in one: the matchit plugin is not copied (FEATURES.md)
    subprocess.run(['nvim', '--clean', '--headless', '--cmd', 'let g:loaded_matchit=1', '-c', SETUP, '-c', 'luafile ' + fl],
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    # Neovim can split a character (Visual A on a one-character multibyte
    # selection writes inside it); keep its bytes so the case shows as a
    # difference instead of stopping the run
    return json.load(open(fo, errors='surrogateescape'))

# Each case gets a fresh Neovim, as it gets a fresh driver: registers, the
# redo buffer, the last insert and the last f/t are global in Neovim.
def nvim(cases):
    with ThreadPoolExecutor(os.cpu_count()) as x:
        return [w for r in x.map(lambda c: nvim1([c]), cases) for w in r]

def ours(drv, c):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(c['text'])
    keys = ' '.join(str(CODES[t]) if t in CODES else str(ord(t)) for k in c['keys'] for t in re.findall('<[^<>]+>|.', k))
    try: p = subprocess.run([drv, f.name], input=keys.encode(), capture_output=True, timeout=20)
    except subprocess.TimeoutExpired: return {'error': 'timeout after 20 s'}   # a hang differs too
    finally: os.unlink(f.name)
    # a split character must show as a difference, not stop the run
    if p.returncode: return {'error': p.returncode, 'stderr': p.stderr[-2000:].decode(errors='backslashreplace')}
    return json.loads(p.stdout.decode(errors='surrogateescape'))

def known_rule(known, c):
    ks = ''.join(c['keys'])
    for k in known:
        if re.search(k['keys'], ks) and re.search(k.get('text', ''), c['text']):
            return k
    return None

def same(g, w):
    return g.get('lines') == w['lines'] and g.get('cursor') == w['cursor']

def main():
    a = sys.argv[1:]
    drv, mode, path = a[0], None, None
    if '--record' in a: mode, path = 'record', a[a.index('--record') + 1]
    if '--replay' in a: mode, path = 'replay', a[a.index('--replay') + 1]
    seed = None
    kp = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'conform-known.json')
    known = json.load(open(kp)) if mode != 'replay' and os.path.exists(kp) else []
    if mode == 'replay':
        cases = json.load(open(path))
        want, kwant = [c.pop('nvim') for c in cases], []
    else:
        n, seed = int(a[1]), int(a[2]) if len(a) > 2 and a[2][0] != '-' else 1
        r = random.Random(seed)
        cases = [{'text': gen_text(r), 'keys': gen_keys(r)} for _ in range(n)]
        want = nvim(cases + [k['example'] for k in known])
        want, kwant = want[:n], want[n:]
    bad = nknown = 0
    with ThreadPoolExecutor(os.cpu_count()) as x:
        got = list(x.map(lambda c: ours(drv, c), cases + [k['example'] for k in known]))
    got, kgot = got[:len(cases)], got[len(cases):]
    for k, w, g in zip(known, kwant, kgot):
        if known_rule([k], k['example']) is not k or same(g, w):
            bad += 1
            print('KNOWN ENTRY STALE (its example now matches Neovim or not its rule): %s' % k['why'])
    keep = []
    for c, w, g in zip(cases, want, got):
        if same(g, w):
            keep.append(dict(c, nvim=w))
        elif known_rule(known, c):
            nknown += 1
        else:
            bad += 1
            if bad <= 5:
                print('DIFF text=%s keys=%s\n  nvim  %r %r\n  ours  %r %r' % (
                    json.dumps(c['text'], ensure_ascii=False), json.dumps(c['keys'], ensure_ascii=False), w['lines'], w['cursor'], g.get('lines'), g.get('cursor') or g))
    split = sum(1 for w in want if any('\udc80' <= ch <= '\udcff' for l in w['lines'] for ch in l))
    print('%d sequences%s: %d differ from Neovim%s%s' % (len(cases), '' if seed is None else ' (seed %d)' % seed, bad,
          ' (%d where Neovim wrote a split character)' % split if split else '',
          ', %d more are known differences (tests/conform-known.json)' % nknown if nknown else ''))
    if mode == 'record':
        json.dump(keep, open(path, 'w'), ensure_ascii=False, indent=0)
    sys.exit(1 if bad else 0)

main()
