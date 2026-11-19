local function run(keys)
  vim.cmd('enew!'); vim.api.nvim_buf_set_lines(0,0,-1,false,{'a-b c','xyz'})
  vim.fn.setreg('-', 'Q'); vim.fn.setreg('"', 'U'); vim.g.hit = ''
  vim.api.nvim_win_set_cursor(0,{1,0})
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys,true,false,true),'xt',false)
  print(string.format('%-10s -> [%s] col %d hit=%s', keys, table.concat(vim.api.nvim_buf_get_lines(0,0,-1,false),'|'), vim.fn.col('.')-1, vim.g.hit))
end
vim.g.mapleader = ' '
vim.keymap.set('n','-',function() vim.g.hit = vim.g.hit..'oil' end)
vim.keymap.set('n','<leader>ff',function() vim.g.hit = vim.g.hit..'ff' end)
run('"-p'); run('" ffx'); run('"- '); run('2"-p'); run('"a-'); run('2-')
