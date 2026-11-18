-- Oracle for tests/test_tools_pick.c: snacks.picker's own result order.
-- In the test's tree: nvim --clean --headless -l pick.lua SNACKS SOURCE PATTERN...
-- Prints one line per pattern: the files in list order, relative to cwd, "|"-ended.
local snacks, source = arg[1], arg[2]
vim.opt.rtp:prepend(snacks)
vim.env.XDG_DATA_HOME = vim.fn.tempname()   -- only the history below
require("snacks").setup({ picker = { enabled = true } })
vim.cmd("edit src/core/core.c")
local cwd = vim.fn.getcwd() .. "/"
-- a history: match.hh visited three times, READ.md once (the test writes
-- the same store)
local frecency = require("snacks.picker.core.frecency")
for _, f in ipairs({ "src/tools/match.hh", "src/tools/match.hh", "src/tools/match.hh", "docs/READ.md" }) do
  frecency.new():visit({ file = cwd .. f })
end
for i = 3, #arg do
  local opts = source == "grep" and { search = arg[i] } or { pattern = arg[i] }
  local p = Snacks.picker[source](opts)
  vim.wait(3000, function() return not p:is_active() end, 10)
  local out = {}
  for _, it in ipairs(p:items()) do
    local f = it.file or ""
    if f:sub(1, #cwd) == cwd then f = f:sub(#cwd + 1) end
    -- grep: file:line, sorted, as rg lists files in parallel
    out[#out + 1] = (source == "grep" and ("%s:%d"):format(f, it.pos[1]) or f) .. "|"
  end
  if source == "grep" then table.sort(out) end
  io.stdout:write(table.concat(out), "\n")
  p:close()
end
