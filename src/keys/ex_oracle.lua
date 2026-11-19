-- tests/test_keys_ex.c's cases in Neovim: nvim --headless --clean -l
-- src/keys/ex_oracle.lua TEXT CASES OUT I.  Line I of CASES is typed into a
-- buffer holding TEXT's lines, with Davy's 'ignorecase' and 'smartcase'
-- (FEATURES.md 1); OUT gets "line col error", then the buffer's lines
-- joined by \1.  One case a process: Neovim keeps the last search, redo
-- and Visual area from one buffer to the next, and some cases loop after
-- others (not traced).
local text = vim.split(io.open(_G.arg[1]):read('*a'), '\n')
table.remove(text)
local i, k = tonumber(_G.arg[4]), nil
for l in io.lines(_G.arg[2]) do
  i = i - 1
  if i == 0 then k = l break end
end
-- a command line that writes (:x file) writes there, not in the repository
vim.fn.chdir(vim.fs.dirname(_G.arg[3]))
vim.o.ignorecase = true
vim.o.smartcase = true
vim.api.nvim_buf_set_lines(0, 0, -1, false, text)
pcall(vim.api.nvim_feedkeys, k, 'xt', false)
io.open(_G.arg[3], 'w'):write(string.format('%d %d %s\n%s\n', vim.fn.line('.'), vim.fn.col('.') - 1,
                              vim.v.errmsg:match('^E%d+') or '-', table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '\1')))
vim.cmd('qa!')
