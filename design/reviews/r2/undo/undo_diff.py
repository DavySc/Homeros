#!/usr/bin/env python3
"""Differential test: UndoTree.tla's "ok" rules (u, C-r, g-, g+, :undo N) against Neovim.
usage: undo_diff.py NSEQ LEN [seed]   (needs nvim on PATH)
MUT=firstchild|newest|nogotolast breaks the model on purpose, to show the test can fail."""
import random, subprocess, sys, json, tempfile, os
import os as _os; MUT = _os.environ.get('MUT', '')
NSEQ, LEN = int(sys.argv[1]), int(sys.argv[2]); rnd = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
def model(seq):
    parent, last, cur, n, out = {0: 0}, {0: 0}, 0, 0, []
    def path(x):
        p = [x]
        while x: x = parent[x]; p.append(x)
        return p[::-1]
    def goto(t):
        nonlocal cur
        pc, pt = path(cur), path(t); i = 0
        while i < min(len(pc), len(pt)) and pc[i] == pt[i]: i += 1
        if MUT != 'nogotolast':
            for c in pt[i:]: last[parent[c]] = c
        cur = t
    for op in seq:
        if op == 'e': n += 1; parent[n] = cur; last[cur] = n; last[n] = n; cur = n
        elif op == 'u':
            if cur: last[parent[cur]] = cur; cur = parent[cur]
        elif op == 'r':
            ch = [c for c in parent if c and parent[c] == cur]
            if ch: cur = min(ch) if MUT == 'firstchild' else max(ch) if MUT == 'newest' else last[cur]
        elif op == 'g-':
            if cur > 0: goto(cur - 1)
        elif op == 'g+':
            if cur < n: goto(cur + 1)
        else:
            t = int(op[1:])
            if t <= n: goto(t)
        out.append(cur)
    return out
LUA = r'''
local seqs = vim.json.decode(io.open(arg_in):read('*a'))
local function k(keys) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), 'xt', false) end
local res = {}
for _, seq in ipairs(seqs) do
  vim.o.swapfile = false; vim.o.more = false; local ob = vim.api.nvim_get_current_buf(); vim.api.nvim_set_current_buf(vim.api.nvim_create_buf(true, false)); vim.api.nvim_buf_delete(ob, {force = true}); local r, texts, okx = {}, {}, true
  for _, op in ipairs(seq) do
    if op == 'e' then k(':put =1<CR>') elseif op == 'u' then k('u') elseif op == 'r' then k('<C-r>')
    elseif op == 'g-' then k('g-') elseif op == 'g+' then k('g+') else pcall(vim.cmd, 'silent! undo ' .. op:sub(2)) end
    local s = vim.fn.undotree().seq_cur; r[#r + 1] = s
    local t = table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '|')
    if texts[s] and texts[s] ~= t then okx = false end; texts[s] = t
  end
  res[#res + 1] = { r, okx }
end
io.open(arg_out, 'w'):write(vim.json.encode(res)); vim.cmd('qa!')
'''
def gen():
    seq, n = [], 0
    for _ in range(LEN):
        op = rnd.choice(['e', 'e', 'u', 'u', 'r', 'g-', 'g+', 'j'])
        if op == 'e': n += 1
        if op == 'j': op = 'j%d' % rnd.randint(0, max(n, 0))
        seq.append(op)
    return seq
seqs = [gen() for _ in range(NSEQ)]
d = tempfile.mkdtemp(); fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
json.dump(seqs, open(fi, 'w')); open(fl, 'w').write("local arg_in, arg_out = '%s', '%s'\n" % (fi, fo) + LUA)
subprocess.run(['nvim', '--clean', '--headless', '-c', 'luafile ' + fl], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, timeout=100)
res = json.load(open(fo)); bad = 0; shown = 0; inexact = 0; kinds = {}
for seq, (nv, okx) in zip(seqs, res):
    inexact += not okx
    m = model(seq)
    if m != nv:
        bad += 1; i = next(i for i in range(len(seq)) if m[i] != nv[i])
        kinds[seq[i] if seq[i][0] != 'j' else ':undo N'] = kinds.get(seq[i] if seq[i][0] != 'j' else ':undo N', 0) + 1
        if shown < 4: shown += 1; print('DIFF at step', i, 'op', seq[i], ' prefix:', ' '.join(seq[:i + 1]), '\n  model', m[:i + 1], '\n  nvim ', nv[:i + 1])
print('sequences %d, length %d: %d differ from the model; first differing op: %s; nvim same-number-different-text: %d' % (NSEQ, LEN, bad, kinds, inexact))
