-- Review of PR 23: oil's message for a directory copied below itself, two
-- levels down (/tmp/op/sub -> /tmp/op/sub/deep/sub).
-- nvim --clean --headless -l parent.lua OIL MINI
vim.opt.rtp:prepend(arg[1]); vim.opt.rtp:prepend(arg[2])
require("mini.icons").setup()
require("oil").setup({ cleanup_delay_ms = false, columns = { "icon" }, view_options = { show_hidden = true } })
vim.fn.system({ "rm", "-rf", "/tmp/op" }); vim.fn.mkdir("/tmp/op/sub/deep", "p")
local parser, mutator = require("oil.mutator.parser"), require("oil.mutator")
local all = {}
local line
for _, dir in ipairs({ "/tmp/op/", "/tmp/op/sub/deep/" }) do
  vim.cmd("edit oil://" .. dir)
  local buf = vim.api.nvim_get_current_buf()
  vim.wait(3000, function() return not vim.b[buf].oil_rendering and vim.api.nvim_buf_get_lines(buf, 0, 1, false)[1] ~= "" end, 5)
  if dir == "/tmp/op/" then line = vim.api.nvim_buf_get_lines(buf, 1, 2, false)[1]
  else vim.api.nvim_buf_set_lines(buf, -1, -1, true, { line }) end
  for _, l in ipairs(vim.api.nvim_buf_get_lines(buf, 0, -1, false)) do print(dir, l) end
  all[buf] = parser.parse(buf)
end
local ok, acts = pcall(mutator.create_actions_from_diffs, all)
print(ok, ok and vim.inspect(acts) or acts)
for b in pairs(all) do vim.bo[b].modified = false end
