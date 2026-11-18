-- Review of PR 51: Neovim's first use of a language: the time to load the
-- highlights query, and the first parse of a one-line buffer.
-- usage: nvim --headless --clean -l nvim_first.lua LANG
vim.opt.rtp:prepend('build/syntax/src/nvim')
local buf = vim.api.nvim_get_current_buf()
vim.api.nvim_buf_set_lines(buf, 0, -1, false, { 'x' })
local t0 = vim.uv.hrtime()
local q = vim.treesitter.query.get(arg[1], 'highlights')
local t1 = vim.uv.hrtime()
vim.treesitter.get_parser(buf, arg[1]):parse(true)
local t2 = vim.uv.hrtime()
print(string.format('%s: Neovim loads the highlights query in %.2f ms, first parse %.2f ms', arg[1], (t1 - t0) / 1e6, (t2 - t1) / 1e6))
