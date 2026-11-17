-- Review of PR 23: snacks' buffers picker order, for pick_probe's "buffers".
-- In a tree: nvim --clean --headless -l buffers.lua SNACKS FILE... -- PATTERN...
local snacks = arg[1]
vim.opt.rtp:prepend(snacks)
vim.env.XDG_DATA_HOME = vim.fn.tempname()
require("snacks").setup({ picker = { enabled = true } })
local i = 2
while arg[i] ~= "--" do vim.cmd("edit " .. arg[i]); vim.uv.sleep(1100); i = i + 1 end   -- lastused has 1 s resolution
local cwd = vim.fn.getcwd() .. "/"
for j = i + 1, #arg do
  local p = Snacks.picker.buffers({ pattern = arg[j] })
  vim.wait(3000, function() return not p:is_active() end, 10)
  local out = {}
  for _, it in ipairs(p:items()) do
    local f = it.file or ""
    if f:sub(1, #cwd) == cwd then f = f:sub(#cwd + 1) end
    out[#out + 1] = f
  end
  io.stdout:write(("%-6s "):format('"' .. arg[j] .. '"'), table.concat(out, " | "), "\n")
  p:close()
end
