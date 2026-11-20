-- Records my-nvim's save kitten (lua/config/core/kitten.lua, path in
-- $KITTEN) for tests/test_kitten.py: plays it for $NAME on an $R x $C
-- screen and prints the three rows above the status line and the command
-- line at each frame, a cell per field ("\1" between cells, "\2" between frames).
local K = dofile(vim.env.KITTEN)
local R, C = tonumber(vim.env.R), tonumber(vim.env.C)
vim.o.lines, vim.o.columns = R, C
vim.opt.shortmess:append('I')   -- no intro screen under the kitten
vim.cmd('enew | redraw!')
local function grab()
  vim.cmd.redraw()
  local t = {}
  for r = R - 4, R - 2 do
    local s = {}
    for c = 1, C do s[#s + 1] = vim.fn.screenstring(r, c) end
    t[#t + 1] = table.concat(s, "\1")
  end
  return table.concat(t, "\n")
end
local blank = grab()
K.paw(vim.env.NAME)
local out, last = {}, blank
while K.playing() do
  vim.wait(4000, function() return grab() ~= last or not K.playing() end, 1)
  if K.playing() and grab() ~= last then
    last = grab()
    out[#out + 1] = last
  end
end
io.stdout:write(table.concat(out, "\n\2\n") .. "\n")
vim.cmd('qa!')
