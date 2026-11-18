#!/usr/bin/env python3
"""Review of PR 52: random Visual selections, deletes, joins and undo, in
nvim --headless and in the driver (marks_drv.c); compares the lines, the
cursor, and '< and '> (lines; columns too unless the selection was linewise,
where getpos() gives 0 and MAXCOL).  Each sequence also runs with `gv <Esc>`
and `gv o <Esc>` appended, which show both ends as the cursor.
usage: marks.py DRIVER N SEED [--gen wide|focus] [--dump FILE]
       marks.py DRIVER --grid [--dump FILE]   (every mark place x start x delete x undo/redo, one text)
       marks.py DRIVER --case TEXT KEY...
       marks.py DRIVER --reduce TEXT KEY...   (drops keys and lines while it differs)
Exit 1 on any difference."""
import json, os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

CODES = {'<Esc>': 27, '<CR>': 13, '<BS>': 127, '<C-r>': 18, '<C-v>': 22, '<C-o>': 15}
WORDS = ['foo', 'bar', 'x', 'ab,', '(c)', 'é', 'de.', '', 'qq']
MOT = ['h', 'j', 'k', 'l', 'w', 'b', 'e', '$', '0', '^', 'gg', 'G', 'j', 'k', 'w', '}', '{']
DMOT = ['w', 'w', 'e', 'j', 'k', '$', '}', '{', 'G', 'gg', 'b', 'l', 'h', 'W', 'ge', 'ap', 'ip', 'iw']
INS = list('ab ,') + ['<CR>', '<BS>']

def gen_text(r):
    lines = []
    for _ in range(r.randint(2, 7)):
        lines.append(r.choice(['', '', '  ']) + ' '.join(r.choice(WORDS) for _ in range(r.randint(0, 4))))
    return '\n'.join(lines) + '\n'

def cnt(r, keys, p=.5, hi=6):
    if r.random() < p: keys.append(str(r.randint(2, hi)))

def ins(r, keys):
    keys += [r.choice(INS) for _ in range(r.randint(0, 3))]
    keys.append('<Esc>')

def gen_keys(r, wide):
    keys = []
    for _ in range(r.randint(2, 9)):
        x = r.random()
        if x < .22:      # a selection, left with Esc: sets '< and '>
            keys.append(r.choice(['v', 'v', 'V', '<C-v>'] if wide else ['v']))
            for _ in range(r.randint(0, 3)):
                cnt(r, keys, .2, 3)
                keys.append(r.choice(MOT))
            keys.append('<Esc>')
        elif x < .4:
            cnt(r, keys, .2, 3)
            keys.append(r.choice(MOT))
        elif x < .62:    # a delete or change over a motion
            op = r.choice(['d', 'd', 'd', 'c'])
            cnt(r, keys, .2, 3)
            keys.append(op)
            if wide and r.random() < .1: keys.append(r.choice(['v', 'V']))
            cnt(r, keys, .6)
            keys.append(r.choice(DMOT))
            if op == 'c': ins(r, keys)
        elif x < .7:     # a Visual delete or change
            keys.append(r.choice(['v', 'v', 'V'] if wide else ['v']))
            for _ in range(r.randint(1, 3)):
                cnt(r, keys, .2, 3)
                keys.append(r.choice(MOT))
            op = r.choice(['d', 'd', 'c', 'J', 'gJ', 'y'] if wide else ['d', 'c', 'J'])
            keys.append(op)
            if op == 'c': ins(r, keys)
        elif x < .8:
            cnt(r, keys, .5, 4)
            keys.append(r.choice(['J', 'gJ']))
        elif x < .9:
            cnt(r, keys, .15, 3)
            keys.append(r.choice(['u', 'u', '<C-r>']))
        elif x < .95:
            cnt(r, keys, .3, 3)
            keys.append(r.choice(['x', 'dd', 'D', 'p', 'P', 'X']))
        else:
            keys.append(r.choice(['i', 'a', 'o', 'O', 'A']))
            ins(r, keys)
    return keys

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
local F = OUT .. '.txt'
local f = io.open(F, 'wb')
f:write(c.text)
f:close()
vim.cmd('silent edit! ' .. vim.fn.fnameescape(F))
os.remove(F)
vim.api.nvim_win_set_cursor(0, {1, 0})
local ks = table.concat(vim.tbl_map(function(k) return k == '<' and '<lt>' or k end, c.keys))
vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(ks, true, true, true), 'xt', false)
local lt, gt = vim.fn.getpos("'<"), vim.fn.getpos("'>")
io.open(OUT, 'w'):write(vim.json.encode({lines = vim.api.nvim_buf_get_lines(0, 0, -1, false),
  cursor = vim.api.nvim_win_get_cursor(0), lt = {lt[2], lt[3]}, gt = {gt[2], gt[3]}, vmode = vim.fn.visualmode()}))
vim.cmd('qa!')
'''
SETUP = ('set noswapfile nomore expandtab shiftwidth=4 softtabstop=4 autoindent smarttab '
         'nostartofline backspace=indent,eol,start tabstop=8 virtualedit=block nowrap shortmess+=I scrolloff=6')

def nvim(c):
    d = tempfile.mkdtemp()
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump(c, open(fi, 'w'))
    open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '--cmd', 'let g:loaded_matchit=1', '-c', SETUP, '-c', 'luafile ' + fl],
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    return json.load(open(fo))

def ours(drv, c):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(c['text'])
    keys = ' '.join(str(CODES[t]) if t in CODES else str(ord(t)) for k in c['keys'] for t in re.findall('<[^<>]+>|.', k))
    try: p = subprocess.run([drv, f.name], input=keys.encode(), capture_output=True, timeout=20)
    except subprocess.TimeoutExpired: return {'error': 'timeout'}
    finally: os.unlink(f.name)
    if p.returncode: return {'error': p.returncode, 'stderr': p.stderr[-1500:].decode(errors='replace')}
    return json.loads(p.stdout)

def diff(w, g):
    """what differs: '' if nothing"""
    if 'error' in g: return 'error'
    if g['lines'] != w['lines']: return 'lines'
    if g['cursor'] != w['cursor']: return 'cursor'
    if w['lt'][0] != g['lt'][0] or w['gt'][0] != g['gt'][0]: return 'mark line'
    # undo brings back a linewise selection without changing visualmode()
    if w['vmode'] != 'V' and w['gt'][1] != 2147483647 and w['lt'][0] and (w['lt'] != g['lt'] or w['gt'] != g['gt']): return 'mark col'
    return ''

def run(drv, cases):
    with ThreadPoolExecutor(os.cpu_count()) as x:
        want = list(x.map(nvim, cases))
        got = list(x.map(lambda c: ours(drv, c), cases))
    return want, got

def main():
    a = sys.argv[1:]
    drv = a[0]
    if a[1] == '--case':
        c = {'text': a[2], 'keys': a[3:]}
        (w,), (g,) = run(drv, [c])
        print('nvim ', w, '\nours ', g, '\n' + (diff(w, g) or 'same'))
        sys.exit(1 if diff(w, g) else 0)
    if a[1] == '--reduce':
        text, keys = a[2], a[3:]
        def differs(t, k):
            try:
                (w,), (g,) = run(drv, [{'text': t, 'keys': k}])
            except Exception: return False
            return diff(w, g) not in ('', 'error')
        assert differs(text, keys)
        changed = True
        while changed:
            changed = False
            for i in range(len(keys) - 1, -1, -1):
                k2 = keys[:i] + keys[i + 1:]
                if differs(text, k2): keys, changed = k2, True
            ls = text.split('\n')
            for i in range(len(ls) - 2, -1, -1):
                t2 = '\n'.join(ls[:i] + ls[i + 1:])
                if differs(t2, keys): text, ls, changed = t2, ls[:i] + ls[i + 1:], True
        print(json.dumps(text, ensure_ascii=False), ' '.join(keys))
        return
    if a[1] == '--grid':
        text = 'abc def\n  xyz uvw\nfoo bar\n\nbar baz qq\nlast\n'
        dels = [['d', '2', 'w'], ['d', '5', 'w'], ['d', '3', 'e'], ['d', '7', 'w'], ['v', '2', 'j', 'd'], ['v', '2', 'j', '$', 'd'],
                ['v', 'j', 'h', 'd'], ['v', '3', 'j', '0', 'd'], ['d', '}'], ['d', 'v', '2', 'j'], ['d', '2', 'j'], ['d', 'G'],
                ['c', '5', 'w', 'x', '<Esc>'], ['v', '2', 'j', 'c', 'x', '<Esc>'], ['3', 'J'], ['3', 'gJ'], ['v', '2', 'j', 'J'],
                ['v', '3', 'j', 'gJ'], ['d', '2', 'j', '.'], ['d', '4', 'w', '.'], ['J', 'J'], ['x', 'd', '4', 'w']]
        cases = []
        for ml in range(1, 6):
            for sel in (['v'], ['l', 'l', 'v', 'j'], ['$', 'v', 'k', '0']):
                for sl in (1, 2):
                    for sc in (['0'], ['w'], ['$']):
                        for d in dels:
                            for tail in ([], ['u'], ['u', '<C-r>'], ['u', '<C-r>', 'u']):
                                cases.append({'text': text, 'keys': [str(ml), 'G'] + sel + ['<Esc>', str(sl), 'G'] + sc + d + tail})
        want, got = run(drv, cases)
        bad = [dict(c, nvim=w, ours=g, what=diff(w, g)) for c, w, g in zip(cases, want, got) if diff(w, g)]
        kinds = {}
        for b in bad: kinds[b['what']] = kinds.get(b['what'], 0) + 1
        for b in bad[:6]:
            print('DIFF (%s) keys=%s\n  nvim %s\n  ours %s' % (b['what'], ' '.join(b['keys']),
                  {k: v for k, v in b['nvim'].items() if k != 'lines'}, {k: v for k, v in b['ours'].items() if k != 'lines'}))
        print('%d grid cases: %d differ %s' % (len(cases), len(bad), kinds or ''))
        if '--dump' in a: json.dump(bad, open(a[a.index('--dump') + 1], 'w'), ensure_ascii=False, indent=0)
        sys.exit(1 if bad else 0)
    n, seed = int(a[1]), int(a[2])
    wide = '--gen' not in a or a[a.index('--gen') + 1] == 'wide'
    r = random.Random(seed)
    cases = []
    for _ in range(n):
        t, k = gen_text(r), gen_keys(r, wide)
        for tail in ([], ['gv', '<Esc>'], ['gv', 'o', '<Esc>']):
            cases.append({'text': t, 'keys': k + tail})
    want, got = run(drv, cases)
    kinds, bad = {}, []
    for c, w, g in zip(cases, want, got):
        d = diff(w, g)
        if d:
            kinds[d] = kinds.get(d, 0) + 1
            bad.append(dict(c, nvim=w, ours=g, what=d))
    for b in bad[:4]:
        print('DIFF (%s) text=%s keys=%s\n  nvim %s\n  ours %s' % (b['what'], json.dumps(b['text'], ensure_ascii=False),
              json.dumps(b['keys'], ensure_ascii=False), {k: v for k, v in b['nvim'].items() if k != 'lines'} , {k: v for k, v in b['ours'].items() if k != 'lines'}))
    print('%d cases (%d sequences x 3, seed %d, %s): %d differ %s' % (len(cases), n, seed, 'wide' if wide else 'focus', len(bad), kinds or ''))
    if '--dump' in a: json.dump(bad, open(a[a.index('--dump') + 1], 'w'), ensure_ascii=False, indent=0)
    sys.exit(1 if bad else 0)

main()
