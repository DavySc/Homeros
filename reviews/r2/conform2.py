#!/usr/bin/env python3
"""Differential test of the undo tree (src/undo) against Neovim.
usage: undo_conform.py "CC FLAGS" SOURCES... -- NSEQ LEN LEVELS [seed]
Builds tests/undo_drv.c with SOURCES and -DUNDO_LEVELS=LEVELS, generates NSEQ
random sequences of LEN ops (edits, u, Ctrl-R, g-, g+, :undo N, :earlier and
:later by count, seconds and writes, writes, clock steps), runs each in
nvim --headless (swapfile off, 'undolevels' LEVELS, time() faked through
LD_PRELOAD so both sides share one clock) and in the driver, and compares the
state number and the text after every op.  Exit 1 on any difference.
LEVELS >= 1: 'undolevels' 0 and below (no undo, or u toggling) are left out,
as nobody uses them (FEATURES.md: 1,000)."""
import json, os, random, subprocess, sys, tempfile

LUA = r'''
local seqs = vim.json.decode(io.open(IN):read('*a'))
local function k(keys) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), 'xt', false) end
local CMD = { j = 'undo %d', n = '%s %d', s = '%s %ds', f = '%s %df' }
local res = {}
vim.o.swapfile = false; vim.o.more = false; vim.o.undolevels = LEVELS
for i, seq in ipairs(seqs) do
  local old = vim.api.nvim_get_current_buf()
  local b = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_set_current_buf(b); vim.api.nvim_buf_delete(old, {force = true})
  vim.api.nvim_buf_set_name(b, DIR .. '/f' .. i)
  local clock, K, r = 1700000000, 0, {}
  vim.env.HOMEROS_TIME = tostring(clock)
  for _, op in ipairs(seq) do
    local c, n = op[1], op[2]
    local lines = vim.api.nvim_buf_line_count(0)
    if c == 'e' then K = K + 1; k(':' .. (n % (lines + 1)) .. "put ='" .. K .. "'<CR>")
    elseif c == 'd' then if lines > 1 then k(':' .. (n % lines + 1) .. 'd<CR>') end
    elseif c == 'c' then k(':' .. (n % lines + 1) .. 's/^/x/<CR>')
    elseif c == 'z' then k(':' .. (n % lines + 1) .. 's/^//<CR>')
    elseif c == 'u' then k('u') elseif c == 'r' then k('<C-r>')
    elseif c == 'w' then vim.cmd('silent write!')
    elseif c == 't' then clock = clock + n; vim.env.HOMEROS_TIME = tostring(clock)
    elseif c == 'j' then pcall(vim.cmd, 'silent! undo ' .. n)
    else vim.cmd('silent ' .. string.format(CMD[c], n < 0 and 'earlier' or 'later', math.abs(n))) end
    r[#r + 1] = vim.fn.undotree().seq_cur .. ' ' .. table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '|')
  end
  res[#res + 1] = r
end
io.open(OUT, 'w'):write(vim.json.encode(res)); vim.cmd('qa!')
'''

def gen(rnd, n):
    # review: a different mix from the package's own: jumps over the whole
    # number range (the package's :undo N only counts 'e' edits), longer
    # g-/g+ runs, larger clock steps and time moves, more writes
    seq, made = [], 0
    mix = rnd.choice(['eeedczuurr--++jjnnsswwfftt', 'eeeeeeedcuuuurrjjjjnn--++', 'eedczuurrsssstttwwwffffjn', 'eeeeeeeeeeeeuurrjnsfwt'])
    for _ in range(n):
        c = rnd.choice(mix)
        if c in 'edcz': v = rnd.randint(0, 99); made += 1
        elif c in '-+': c, v = 'n', -1 if c == '-' else 1
        elif c == 'j': v = rnd.randint(0, made + 2)
        elif c == 'n': v = rnd.choice([-1, 1]) * rnd.randint(1, 30)
        elif c == 's': v = rnd.choice([-1, 1]) * rnd.randint(1, 40)
        elif c == 'f': v = rnd.choice([-1, 1]) * rnd.randint(1, 6)
        elif c == 't': v = rnd.choice([0, 0, 1, 1, 2, 3, 7, 20])
        else: v = 0
        seq.append([c, v])
    return seq

def main():
    i = sys.argv.index('--')
    cc, srcs = sys.argv[1].split(), sys.argv[2:i]
    nseq, length, levels = int(sys.argv[i + 1]), int(sys.argv[i + 2]), int(sys.argv[i + 3])
    seed = int(sys.argv[i + 4]) if len(sys.argv) > i + 4 else 1
    here = os.environ['HOMEROS_TOOLS']
    with tempfile.TemporaryDirectory() as d:
        return run(cc, srcs, nseq, length, levels, seed, here, d)

def run(cc, srcs, nseq, length, levels, seed, here, d):
    drv, shim = d + '/undo_drv', d + '/fake_time.so'
    subprocess.run(cc + ['-DUNDO_LEVELS=%d' % levels, '-o', drv, os.path.dirname(here) + '/tests/undo_drv.c'] + srcs, check=True)
    subprocess.run(['cc', '-shared', '-fPIC', '-O1', '-o', shim, here + '/fake_time.c'], check=True)
    rnd = random.Random(seed)
    seqs = [gen(rnd, length) for _ in range(nseq)]
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump(seqs, open(fi, 'w'))
    open(fl, 'w').write("local IN, OUT, DIR, LEVELS = '%s', '%s', '%s', %d\n" % (fi, fo, d, levels) + LUA)
    env = dict(os.environ, LD_PRELOAD=shim, HOMEROS_TIME='1700000000')
    subprocess.run(['nvim', '--clean', '--headless', '-n', '-c', 'luafile ' + fl], env=env, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, timeout=3000)
    nv = json.load(open(fo))
    if len(nv) != nseq or any(len(r) != len(q) for r, q in zip(nv, seqs)):
        print('undo_conform: FAIL, nvim did not run every op'); return 1
    inp = ''.join('-\n' + ''.join('%s %d\n' % (c, v) for c, v in s) for s in seqs)
    p = subprocess.run([drv], input=inp, capture_output=True, text=True)
    if p.returncode:
        print(p.stderr[-2000:]); print('undo_conform: FAIL, the driver stopped (exit %d)' % p.returncode); return 1
    out = p.stdout.split('\n')
    bad, at = 0, 0
    for s, r in zip(seqs, nv):
        mine = out[at:at + len(s)]; at += len(s)
        if mine != r:
            j = next(j for j in range(len(s)) if mine[j] != r[j])
            if bad < 3:
                print('DIFF at op %d %s; ops: %s' % (j, s[j], ' '.join('%s%d' % (c, v) for c, v in s[:j + 1])))
                print('  nvim  ', r[max(0, j - 2):j + 1]); print('  undo.c', mine[max(0, j - 2):j + 1])
            bad += 1
    print('undo_conform: %d sequences of %d ops, undolevels %d, seed %d: %d differ from Neovim' % (nseq, length, levels, seed, bad))
    return 1 if bad else 0

sys.exit(main())
