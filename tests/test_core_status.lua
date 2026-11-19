-- The oracle for tests/test_core_status.c: mini.statusline's default
-- content (my-nvim's setup, without icons) as Neovim lays it out, through
-- nvim_eval_statusline(), with HOME the file's directory.  Prints one line
-- per state: the text, then each highlight's start byte and group.
-- usage, in an empty directory holding f0 (test_core_status.c's):
--   HOME=$PWD nvim --headless --clean -c 'luafile test_core_status.lua' -c 'qa!'
-- with MINI the path of mini.nvim's lua/mini/statusline.lua.
local MS = dofile(os.getenv('MINI'))
MS.setup({ use_icons = false })
vim.o.laststatus = 2
local function show(tag, w)
  w = w or vim.api.nvim_get_current_win()
  vim.g.actual_curwin = vim.api.nvim_get_current_win()
  local r = vim.api.nvim_eval_statusline(vim.go.statusline, { winid = w, highlights = true, maxwidth = vim.api.nvim_win_get_width(w) })
  local hl = {}
  for _, h in ipairs(r.highlights) do hl[#hl + 1] = h.start .. ':' .. h.group end
  io.stdout:write(tag .. ' [' .. r.str .. '] ' .. table.concat(hl, ' ') .. '\n')
end
vim.cmd('e f0')
for _, c in ipairs({ 200, 140, 139, 120, 119, 80, 75, 74, 40, 12 }) do
  vim.o.columns = c
  show('width ' .. c)
end
vim.o.columns = 80
vim.cmd('vsplit')
for _, c in ipairs({ 11, 8, 6, 5, 4, 3, 2, 1 }) do
  vim.api.nvim_win_set_width(0, c)
  show('width ' .. c)
end
show('other', vim.fn.win_getid(vim.fn.winnr('l')))
vim.cmd('only')
vim.api.nvim_win_set_cursor(0, { 2, 1 })
show('2,1')
vim.api.nvim_win_set_cursor(0, { 3, 0 })
show('3,0')
vim.cmd('normal! x')
show('x')
vim.o.columns = 130
show('x, 130')
vim.cmd('w | e new')
show('new')
vim.fn.writefile({ 'ab\r', 'cd\r' }, 'd1')
vim.cmd('e d1')
show('d1')
vim.fn.writefile(vim.fn['repeat']({ '123456789' }, 200), 'k1')
vim.cmd('e k1')
show('k1')
