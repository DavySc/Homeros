-- CurSearch in a window that is not the current one (Neovim): in each
-- window, is the match under that window's cursor drawn unlike the others?
-- usage: nvim --clean --headless -c 'luafile split.lua'
vim.o.hlsearch = true
vim.api.nvim_buf_set_lines(0, 0, -1, false, { 'ab ab ab ab' })
vim.cmd('normal! /ab\r')
vim.cmd('normal! gg0')
vim.cmd('split | normal! 3l')
vim.cmd('redraw!')
for _, w in ipairs(vim.api.nvim_list_wins()) do
  local p, s = vim.fn.win_screenpos(w), {}
  for c = 1, 11 do s[#s + 1] = vim.fn.screenattr(p[1], c) end
  io.stdout:write((w == vim.api.nvim_get_current_win() and 'current' or 'other  ') .. ' window, cursor col ' .. vim.api.nvim_win_get_cursor(w)[2] .. ': attrs ' .. table.concat(s, ' ') .. '\n')
end
vim.cmd('qa!')
