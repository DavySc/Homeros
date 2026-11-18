local r = {}
vim.cmd('redraw')
r.after_redraw = vim.fn.screenstring(28, 1)
vim.cmd('redraw!')
r.after_redraw_bang = vim.fn.screenstring(28, 1)
r.mode = vim.fn.mode()
return r
