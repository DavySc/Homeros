-- Review of PR 120: 100 one-character diagnostics side by side on line 1;
-- how many cells does Neovim underline?  nvim --clean --headless -c "luafile cap64.lua"
vim.o.lines = 40 vim.o.columns = 120 vim.o.wrap = false
vim.api.nvim_buf_set_lines(0, 0, -1, false, {string.rep('x', 200), string.rep('x', 200), string.rep('x', 200)})
local d = {}
for k = 0, 99 do d[#d + 1] = {lnum = 0, col = k, end_lnum = 0, end_col = k + 1, severity = 1, message = 'm'} end
vim.api.nvim_set_hl(0, 'DiagnosticUnderlineError', {undercurl = true, sp = 1})
vim.diagnostic.set(vim.api.nvim_create_namespace('t'), 0, d)
vim.api.nvim_win_set_cursor(0, {3, 0})
vim.cmd('redraw!')
local off = vim.fn.getwininfo(vim.fn.win_getid())[1].textoff
local plain, n = vim.fn.screenattr(2, off + 1), 0
for c = off + 1, 120 do if vim.fn.screenattr(1, c) ~= plain then n = n + 1 end end
local a = {} for c = 1, 120 do a[#a+1] = vim.fn.screenattr(1, c) end
io.stdout:write(('Neovim underlines %d cells of line 1 (textoff %d)\n'):format(n, off))
vim.cmd('qa!')
