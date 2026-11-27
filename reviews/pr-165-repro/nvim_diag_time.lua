-- PR 165 review: Neovim's ]d on the same case as fake.c's (LINES lines "int x;", a diagnostic at column 4 of each of the first ND lines)
local LINES, ND = tonumber(vim.env.LINES_), tonumber(vim.env.ND)
local l = {}
for i = 1, LINES do l[i] = 'int x;' end
vim.api.nvim_buf_set_lines(0, 0, -1, false, l)
local ns, d = vim.api.nvim_create_namespace('t'), {}
for i = 0, ND - 1 do d[#d + 1] = { lnum = i, col = 4, end_lnum = i, end_col = 5, severity = 1, message = 'm' } end
vim.diagnostic.set(ns, 0, d)
local out = {}
for r = 1, 3 do
  vim.api.nvim_win_set_cursor(0, { ND + 11, 0 })
  local t = vim.uv.hrtime()
  vim.api.nvim_feedkeys(']d', 'x', false)
  local p = vim.api.nvim_win_get_cursor(0)
  out[#out + 1] = string.format(']d %.1f ms -> %d,%d', (vim.uv.hrtime() - t) / 1e6, p[1] - 1, p[2])
end
io.open(vim.env.OUT, 'w'):write(table.concat(out, '\n') .. '\n')
vim.cmd('qa!')
