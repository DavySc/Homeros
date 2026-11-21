-- Review of PR 110: Neovim's foldexpr per line for each FILE, as
-- tests/test_syntax.lua's folds() (run from the repo root):
-- nvim --headless --clean -l folds.lua FILE...
vim.opt.rtp:prepend('build/syntax/src/nvim')
vim.g.markdown_folding = 1
for k = 1, #arg do
  vim.cmd.edit(arg[k])
  local buf = vim.api.nvim_get_current_buf()
  local ft = vim.bo[buf].filetype
  local o, prev = {}, 0
  if ft ~= 'markdown' then
    local ok, p = pcall(vim.treesitter.get_parser, buf)
    if ok and p then p:parse(true) end
  end
  for l = 1, vim.api.nvim_buf_line_count(buf) do
    local v
    if ft == 'markdown' then
      vim.v.lnum = l
      v = vim.fn.MarkdownFold()
      if v == '=' then v = tostring(prev) end
    else
      v = vim.treesitter.foldexpr(l)
    end
    prev = tonumber((v:gsub('>', '')))
    o[#o + 1] = v
  end
  io.write(arg[k], ' ', ft, ' ', table.concat(o, ' '), '\n')
  vim.cmd('bwipeout!')
end
