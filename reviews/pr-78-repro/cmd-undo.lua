-- K without snippets: nvim --headless --clean -u NONE -c "luafile cmd-undo.lua" prints { "a", "xe" }
vim.o.swapfile=false
vim.api.nvim_buf_set_lines(0,0,-1,true,{'a','x'})
vim.cmd('normal! gg')
local k=vim.api.nvim_replace_termcodes('iq<Cmd>call cursor(2,2)<CR>e<Esc>u',true,false,true)
vim.api.nvim_feedkeys(k,'xt',false)
print(vim.inspect(vim.api.nvim_buf_get_lines(0,0,-1,true)))
vim.cmd('qa!')
