-- The yank flash where it overlaps 'hlsearch' matches (Neovim), by
-- screenattr() as tests/test_core_visual.py reads it: "ab ab ab ab", /ab,
-- then 0ly3l flashes "b ab" (columns 1-4).
-- usage: nvim --clean --headless -c 'luafile flash_search.lua'
vim.o.hlsearch = true
vim.api.nvim_buf_set_lines(0, 0, -1, false, { 'ab ab ab ab' })
vim.cmd('normal! /ab\r')
vim.api.nvim_create_autocmd('TextYankPost', { callback = function() vim.hl.on_yank({ timeout = 1e8 }) end })
vim.cmd('normal! gg0ly3l')
local function row() vim.cmd('redraw!') local s = {} for c = 1, 11 do s[#s + 1] = string.format('%3d', vim.fn.screenattr(1, c)) end return table.concat(s, ' ') end
io.stdout:write('\nflash + search:  ' .. row() .. '\n')
vim.api.nvim_buf_clear_namespace(0, vim.api.nvim_get_namespaces()['nvim.hlyank'], 0, -1)
io.stdout:write('search only:     ' .. row() .. '\n')
vim.cmd('nohlsearch')
vim.cmd('normal! yl')   -- flashes column 1 only; then the same flash as before
vim.api.nvim_buf_clear_namespace(0, vim.api.nvim_get_namespaces()['nvim.hlyank'], 0, -1)
vim.cmd('normal! y3l')
vim.cmd('nohlsearch')
io.stdout:write('flash only:      ' .. row() .. '\n')
vim.cmd('qa!')
