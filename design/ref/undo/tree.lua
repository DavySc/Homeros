-- Record Neovim's undo tree behaviour: run each key sequence, print text and state.
local function k(keys) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), 'xt', false) end
local function st(label)
  local t = vim.fn.undotree()
  io.stdout:write(string.format('%-28s text=%-22s seq_cur=%d seq_last=%d changenr=%d\n', label,
    table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '|'), t.seq_cur, t.seq_last, vim.fn.changenr()))
end
local steps = {
  {'i one<Esc>', 'insert one'}, {'o two<Esc>', 'o two'}, {'u', 'u'}, {'o three<Esc>', 'o three (branch)'},
  {'u', 'u'}, {'<C-r>', 'C-r (newest branch?)'}, {'g-', 'g-'}, {'g-', 'g-'}, {'g-', 'g-'}, {'g-', 'g- at root'},
  {'g+', 'g+'}, {'g+', 'g+'}, {'g+', 'g+'}, {':undo 2<CR>', ':undo 2'}, {'u', 'u'}, {'<C-r>', 'C-r after :undo 2'},
  {':undo 0<CR>', ':undo 0'}, {':earlier 10<CR>', ':earlier 10 (past root)'}, {':later 99<CR>', ':later 99'},
  {':undo 1<CR>', ':undo 1'}, {'otwo2<Esc>', 'o two2 (3rd branch at 1)'}, {'u', 'u'}, {'<C-r>', 'C-r'},
}
st('start')
for _, s in ipairs(steps) do k(s[1]); st(s[2]) end
local function dump(e, d) for _, x in ipairs(e) do io.stdout:write(string.rep('  ', d) .. 'seq ' .. x.seq .. (x.curhead and ' curhead' or '') .. (x.newhead and ' newhead' or '') .. '\n'); if x.alt then io.stdout:write(string.rep('  ', d) .. ' alt:\n'); dump(x.alt, d + 2) end end end
io.stdout:write('tree:\n'); dump(vim.fn.undotree().entries, 1)
-- Insert-session grouping and cursor moves inside insert mode
vim.cmd('enew!'); k('iab<Left>c<Esc>'); st('insert ab<Left>c'); k('u'); st('u (Left splits group?)')
vim.cmd('enew!'); k('iab<C-g>uc<Esc>'); st('insert ab C-g u c'); k('u'); st('u (C-g u breaks)')
vim.cmd('qa!')
