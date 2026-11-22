-- Review of PR 120: where is the diagnostic's message after the cursor
-- moves in Insert mode (i<Down>), and in Visual mode (vj)?
-- nvim --clean --headless -c "luafile insert.lua"
vim.o.lines = 8 vim.o.columns = 50
vim.api.nvim_buf_set_lines(0, 0, -1, false, {'int x', 'int y', 'int z'})
vim.diagnostic.config({virtual_text = {current_line = true}})
local d = {}
for k = 0, 2 do d[#d + 1] = {lnum = k, col = 0, end_lnum = k, end_col = 3, severity = 1, message = 'msg' .. k} end
vim.diagnostic.set(vim.api.nvim_create_namespace('t'), 0, d)
local function show(what)
  vim.cmd('redraw!')
  local o = {}
  for r = 1, 3 do local s = {} for c = 1, 50 do s[#s + 1] = vim.fn.screenstring(r, c) end o[#o + 1] = (table.concat(s):gsub('%s+$', '')) end
  io.stdout:write(what .. ': ' .. table.concat(o, ' | ') .. '\n')
end
-- typed keys, each step after the loop has run the one before
local steps = {{'gg', 'Normal, line 1'}, {'i<Down>', 'i<Down>'}, {'<Esc>', '<Esc>'}, {'ggvj', 'ggvj'}, {'<Esc>jj', '<Esc>jj'}}
local function step(k)
  if not steps[k] then vim.cmd('qa!') return end
  vim.api.nvim_input(steps[k][1])
  vim.defer_fn(function() show(steps[k][2]) step(k + 1) end, 200)
end
step(1)
