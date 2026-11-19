vim.g.mapleader=' '
vim.keymap.set('n','<leader>ff',function() vim.g.hit=(vim.g.hit or '')..'ff' end)
vim.keymap.set('n','q',function() vim.g.hit=(vim.g.hit or '')..'Q' end,{buffer=true})
vim.api.nvim_buf_set_lines(0,0,-1,false,{'abc'})
vim.api.nvim_feedkeys(' q','xt',false)
print('hit='..tostring(vim.g.hit)..' recording=['..vim.fn.reg_recording()..'] col '..(vim.fn.col('.')-1))
