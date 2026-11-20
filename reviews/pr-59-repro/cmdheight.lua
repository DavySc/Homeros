local K = dofile(vim.env.KITTEN)
vim.o.lines, vim.o.columns = 10, 40
vim.opt.shortmess:append('I')
vim.cmd('enew | resize -2 | redraw!')
K.paw('notes.org')
vim.wait(800, function() return false end)
for _, w in ipairs(vim.api.nvim_list_wins()) do
  local c = vim.api.nvim_win_get_config(w)
  print(w, c.relative, vim.inspect(c.row), vim.inspect(c.col), c.width, c.height, vim.inspect(vim.fn.win_screenpos(w)))
end
print(vim.o.lines, vim.o.columns, vim.o.cmdheight)
vim.cmd('qa!')
