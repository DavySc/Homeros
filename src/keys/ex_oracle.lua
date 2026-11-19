-- tests/test_keys_ex.c's cases in Neovim: nvim --headless --clean -l
-- src/keys/ex_oracle.lua TEXT CASES OUT.  Each line of CASES is typed into
-- a new buffer holding TEXT's lines; OUT gets "line col error" for each.
local text = vim.split(io.open(_G.arg[1]):read('*a'), '\n')
table.remove(text)
local out = io.open(_G.arg[3], 'w')
-- a command line that writes (:x file) writes there, not in the repository
vim.fn.chdir(vim.fs.dirname(_G.arg[3]))
for k in io.lines(_G.arg[2]) do
  vim.cmd('enew!')
  vim.api.nvim_buf_set_lines(0, 0, -1, false, text)
  vim.v.errmsg = ''
  pcall(vim.api.nvim_feedkeys, k, 'xt', false)
  out:write(string.format('%d %d %s\n', vim.fn.line('.'), vim.fn.col('.') - 1, vim.v.errmsg:match('^E%d+') or '-'))
end
out:close()
vim.cmd('qa!')
