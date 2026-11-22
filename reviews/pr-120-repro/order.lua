-- Review of PR 120: which message and sign does Neovim show when the
-- server's order is not by position?  Two diagnostics on line 1, given as
-- {col 4 warn "second-col", col 0 error "first-col"}.
-- nvim --clean --headless -c "luafile order.lua"
vim.o.lines = 8 vim.o.columns = 60
vim.api.nvim_buf_set_lines(0, 0, -1, false, {'int x = 1;', 'int y'})
vim.diagnostic.config({virtual_text = {current_line = true}})
vim.diagnostic.set(vim.api.nvim_create_namespace('t'), 0, {
  {lnum = 0, col = 4, end_lnum = 0, end_col = 5, severity = 2, message = 'second-col'},
  {lnum = 0, col = 0, end_lnum = 0, end_col = 3, severity = 1, message = 'first-col'}})
vim.cmd('redraw') vim.api.nvim_win_set_cursor(0, {1, 0}) vim.api.nvim_exec_autocmds('CursorMoved', {}) vim.cmd('redraw!')
local s = {} for c = 1, 60 do s[#s + 1] = vim.fn.screenstring(1, c) end
io.stdout:write('given col 4 then col 0: ' .. table.concat(s) .. '\n')
vim.cmd('qa!')
