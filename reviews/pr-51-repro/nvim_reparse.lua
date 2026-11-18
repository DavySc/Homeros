-- Review of PR 51: Neovim's own reparse time per key on an SQL file, with the
-- test's parser.  usage: nvim --headless --clean -l nvim_reparse.lua FILE LINE
vim.opt.rtp:prepend('build/syntax/src/nvim')
vim.cmd('edit ' .. arg[1])
local buf = vim.api.nvim_get_current_buf()
vim.treesitter.start(buf, 'sql')
local p = vim.treesitter.get_parser(buf)
p:parse(true)
local row, ms = tonumber(arg[2]) + 25, {}
for k = 1, 220 do
  vim.api.nvim_buf_set_text(buf, row, (k - 1) % 3, row, (k - 1) % 3, { 'x' })
  local t0 = vim.uv.hrtime()
  p:parse(true)
  if k > 20 then ms[#ms + 1] = (vim.uv.hrtime() - t0) / 1e6 end
end
table.sort(ms)
print(string.format('%s line %s: Neovim parse per key: median %.2f ms, p99 %.2f, max %.2f', arg[1], arg[2], ms[101], ms[198], ms[200]))
