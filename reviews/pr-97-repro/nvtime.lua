-- nvtime.lua FILE LINE KEYS: per key typed at LINE+25, the time of the parse of the 50 visible lines
vim.opt.rtp:prepend('build/syntax/src/nvim')
local file, l0, keys = arg[1], tonumber(arg[2]), tonumber(arg[3])
vim.cmd('edit ' .. file)
vim.bo.swapfile = false
local buf = vim.api.nvim_get_current_buf()
vim.treesitter.start(buf, 'nix')
local p = vim.treesitter.get_parser(buf)
p:parse({ l0, l0 + 50 })
local ts = {}
for k = 1, keys do
  vim.api.nvim_buf_set_text(buf, l0 + 25, 0, l0 + 25, 0, { 'x' })
  local t0 = vim.uv.hrtime()
  p:parse({ l0, l0 + 50 })
  ts[#ts + 1] = (vim.uv.hrtime() - t0) / 1e6
end
table.sort(ts)
io.stdout:write(string.format('nvim parse per key: med %.1f max %.1f ms\n', ts[math.floor(#ts / 2) + 1], ts[#ts]))
vim.cmd('qa!')
