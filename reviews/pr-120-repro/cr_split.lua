-- Review of PR 120: (1) a message with "\r"; (2) two windows on one buffer
-- and a window on another buffer: where is the message?
-- nvim --clean --headless -c "luafile cr_split.lua"
vim.o.lines = 14 vim.o.columns = 50
vim.api.nvim_buf_set_lines(0, 0, -1, false, {'int x', 'int y', 'int z'})
vim.diagnostic.config({virtual_text = {current_line = true}})
local d = {}
for k = 0, 2 do d[#d + 1] = {lnum = k, col = 0, end_lnum = k, end_col = 3, severity = 1, message = 'm' .. k .. '\rq\r\nr'} end
vim.diagnostic.set(vim.api.nvim_create_namespace('t'), 0, d)
local function show(what)
  vim.cmd('redraw!')
  local o = {}
  for r = 1, vim.o.lines - 1 do local s = {} for c = 1, 50 do s[#s + 1] = vim.fn.screenstring(r, c) end o[#o + 1] = (table.concat(s):gsub('%s+$', '')) end
  io.stdout:write(what .. ':\n  ' .. table.concat(o, '\n  ') .. '\n')
end
local steps = {{'gg', 'one window, line 1'}, {'<C-w>sjj', 'split, cursor to line 3 in the new (top) window'},
               {'<C-w>j', 'CTRL-W j: the lower window (cursor line 1)'}, {':enew<CR>', ':enew in the lower window'}}
local function step(k)
  if not steps[k] then vim.cmd('qa!') return end
  vim.api.nvim_input(steps[k][1])
  vim.defer_fn(function() show(steps[k][2]) step(k + 1) end, 200)
end
step(1)
