-- Which face wins where two overlap, in Neovim: the yank flash (on_yank),
-- Visual and :s's c-flag match against 'hlsearch'.
-- usage: nvim --clean --headless -c 'luafile prio.lua'  (prints one row of cells per scenario)
vim.o.termguicolors = true
vim.o.hlsearch = true
vim.cmd('hi Search guibg=#110000 guifg=#000011 | hi CurSearch guibg=#440000 guifg=#000044 | hi IncSearch guibg=#002200 guifg=#000022 | hi Visual guibg=#000033 | hi Normal guibg=#000000 guifg=#ffffff')
local names = { [0x110000] = 'S', [0x440000] = 'C', [0x002200] = 'I', [0x000033] = 'V', [0] = '.' }
local fnames = { [0x000011] = 's', [0x000044] = 'c', [0x000022] = 'i', [0xffffff] = '.' }
local function row(r)
  vim.cmd('redraw!')
  local bg, fg = {}, {}
  for c = 0, 11 do
    local a = vim.api.nvim__inspect_cell(1, r, c)[2] or {}
    bg[#bg + 1] = names[a.background or 0] or '?'
    fg[#fg + 1] = fnames[a.foreground or 0xffffff] or '?'
  end
  return table.concat(bg) .. '  fg ' .. table.concat(fg)
end
vim.api.nvim_buf_set_lines(0, 0, -1, false, { 'ab ab ab ab', 'ab ab' })
vim.cmd('normal! /ab\r')
vim.cmd('normal! gg0')
vim.cmd('redraw!')
io.stdout:write('search only, cursor on first:   ' .. row(0) .. '\n')
vim.api.nvim_create_autocmd('TextYankPost', { callback = function() vim.hl.on_yank({ timeout = 1e8 }) end })
vim.cmd('normal! 0ly3l')   -- "b ab" flashed: half of match 1, all of match 2
io.stdout:write('yank flash over cols 1-4:       ' .. row(0) .. '\n')
vim.api.nvim_buf_clear_namespace(0, vim.api.nvim_get_namespaces()['nvim.hlyank'], 0, -1)
vim.cmd('normal! 0lv3l')
io.stdout:write('Visual cols 1-4 (cursor col 4): ' .. row(0) .. '\n')
vim.cmd('normal! \27')
vim.defer_fn(function()
  io.stdout:write(':s c flag, asking about match 2:' .. row(0) .. '\n')
  vim.api.nvim_feedkeys('q', 'n', false)
  vim.defer_fn(function() vim.cmd('qa!') end, 50)
end, 200)
vim.api.nvim_feedkeys(':1s/ab/X/gc\ry', 'n', false)
