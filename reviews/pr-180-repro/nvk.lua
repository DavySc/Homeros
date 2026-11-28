-- interactive: m.c with the fake server attached; keys come from tmux
local dir = vim.fn.tempname(); vim.fn.mkdir(dir)
vim.fn.writefile({'int x;', 'int y;', 'int foo(int x);'}, dir .. '/m.c')
vim.o.swapfile = false; vim.o.hidden = true
if vim.env.PRE ~= '' and vim.env.PRE then vim.fn.writefile({'# t'}, dir .. '/n.md'); vim.cmd('edit ' .. dir .. '/n.md') end
vim.cmd('edit ' .. dir .. '/m.c')
vim.lsp.start({ name = 'fake', cmd = { 'python3', vim.env.FAKE, dir .. '/log' }, root_dir = dir })
