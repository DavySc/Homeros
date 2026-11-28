#!/usr/bin/env python3
"""The diagnostic jumps (]d [d ]D [D 9]d) on a long file, Neovim 0.12's
vim.diagnostic.jump() against Homeros's (diags.c, built as
build/bench/diags): N diagnostics on random lines of 100,000, the cursor
in the middle, RUNS alternating runs of each, median and range in ms.
python3 src/lsp/bench/diags.py N RUNS (nvim on PATH, from the repo)."""
import json, os, random, statistics, subprocess, sys, tempfile

n, runs = int(sys.argv[1]), int(sys.argv[2])
d, r = tempfile.mkdtemp(), random.Random(7)
lines = ['int x%d = %d;' % (i, i) for i in range(100000)]
t = '\n'.join(lines) + '\n'
at = [0]
for l in lines: at.append(at[-1] + len(l) + 1)
ds = sorted(at[i] + r.randrange(4) for i in r.sample(range(len(lines)), n))
open(d + '/m.c', 'w').write(t)
# the fake server's case: test_lsp_locs.py --serve publishes ds on didOpen
open(d + '/t.txt', 'w').write(json.dumps({'k': 0, 'kind': 'diag', 'files': {'m.c': t}, 'diags': [[a, a + 1] for a in ds]}) + '\n')
nd = [{'lnum': t.count('\n', 0, a), 'col': a - t.rfind('\n', 0, a) - 1, 'severity': 1, 'message': 'm'} for a in ds]
open(d + '/t.lua', 'w').write('''
vim.diagnostic.set(vim.api.nvim_create_namespace('b'), 0, vim.json.decode(%r))
local out = {}
for _, k in ipairs({{1, true}, {-1, true}, {math.huge, false}, {-math.huge, false}, {9, true}}) do
  vim.api.nvim_win_set_cursor(0, {50001, 0})
  local t = vim.uv.hrtime()
  vim.diagnostic.jump({count = k[1], wrap = k[2]})
  out[#out + 1] = string.format('%%.3f %%d', (vim.uv.hrtime() - t) / 1e6, vim.api.nvim_win_get_cursor(0)[1] - 1)
end
vim.fn.writefile(out, %r)
vim.cmd('qa!')
''' % (json.dumps(nd), d + '/nv.txt'))
keys, tm = [']d', '[d', ']D', '[D', '9]d'], {'nvim': [[] for _ in range(5)], 'homeros': [[] for _ in range(5)]}
for _ in range(runs):
    subprocess.run(['nvim', '--clean', '--headless', '-c', 'set noswapfile', '-c', 'luafile ' + d + '/t.lua', d + '/m.c'],
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, timeout=600, check=True)
    nv = [l.split() for l in open(d + '/nv.txt')]
    ho = subprocess.run(['build/bench/diags', d + '/t.txt', d + '/m.c', str(n)], capture_output=True, text=True, check=True).stdout.split('\n')
    for i in range(5):
        h = ho[i].split()
        if h[1] != nv[i][1]: sys.exit('%s: nvim line %s, homeros %s' % (keys[i], nv[i][1], h[1]))
        tm['nvim'][i].append(float(nv[i][0]))
        tm['homeros'][i].append(float(h[0]))
for i, k in enumerate(keys):
    print(k, *('%s %.3f (%.3f to %.3f)' % (s, statistics.median(v[i]), min(v[i]), max(v[i])) for s, v in tm.items()))
