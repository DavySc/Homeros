local r = {}
local function status()
  local s = {}
  for c = 1, vim.o.columns do s[#s+1] = vim.fn.screenstring(vim.fn.win_screenpos(0)[1] + vim.fn.winheight(0), c) end
  return (table.concat(s):gsub('%s+$', ''))
end
r.status0 = status()
r.pos = vim.fn.getpos('.')
r.curswant = vim.fn.getcurpos()[5]
r.screencol = vim.fn.screencol()
r.line = vim.fn.getline('.')
r.virtcol = vim.fn.virtcol('.')
vim.cmd('redraw!')
r.status_after_reading_virtcol = status()
return r
