-- Review of PR 110: Neovim's side of foldcost.c.  One-byte edits at random
-- places with foldmethod=expr and vim.treesitter.foldexpr(): the time the
-- edit itself holds the editor (fold.c asks foldexpr for the changed lines),
-- and the CPU time of the edit, the reparse and the fold levels after it.
-- nvim --headless --clean -l foldcost.lua FILE [edits]   (from the repo root)
vim.opt.rtp:prepend('build/syntax/src/nvim')
vim.cmd('silent edit ' .. arg[1])
vim.bo.readonly = false
vim.treesitter.start()
vim.wo.foldmethod = 'expr'
vim.wo.foldexpr = 'v:lua.vim.treesitter.foldexpr()'
vim.treesitter.get_parser():parse(true)
vim.cmd('normal! zx')
vim.wait(300)
local E, r = tonumber(arg[2] or 200), 1
local n = vim.api.nvim_buf_line_count(0)
local key, cpu = {}, {}
local function hr() return vim.uv.hrtime() / 1e6 end
for e = 1, E do
  r = (r * 1103515245 + 12345) % 4294967296
  local row = math.floor(r / 256) % n
  local c0, t0 = os.clock(), hr()
  vim.api.nvim_buf_set_text(0, row, 0, row, 0, { ' ' })
  vim.fn.foldlevel(row + 1)
  key[e] = hr() - t0
  -- headless nothing redraws, so nothing parses: parse as the highlighter
  -- would for a redraw, then let the fold callbacks run
  vim.treesitter.get_parser():parse()
  vim.wait(60)
  cpu[e] = (os.clock() - c0) * 1e3
  vim.api.nvim_buf_set_text(0, row, 0, row, 1, {})
  vim.treesitter.get_parser():parse()
  vim.wait(60)
end
table.sort(key) table.sort(cpu)
local function q(t, p) return t[math.max(1, math.ceil(#t * p))] end
io.write(string.format('%s: per edit: held by the edit median %.3f p99 %.3f max %.3f ms; CPU for the edit, the reparse and the folds median %.3f p99 %.3f max %.3f ms; foldlevel(last)=%d\n',
  arg[1], q(key, .5), q(key, .99), key[#key], q(cpu, .5), q(cpu, .99), cpu[#cpu], vim.fn.foldlevel(n)))
-- the folds do follow an edit: a brace opened on line 1 raises the last line's level
vim.api.nvim_buf_set_text(0, 0, 0, 0, 0, { 'void zz() {' })
vim.treesitter.get_parser():parse()
vim.wait(300)
io.write('after an opened brace: foldlevel(last)=', vim.fn.foldlevel(n), '\n')
