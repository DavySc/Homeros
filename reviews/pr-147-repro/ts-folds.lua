vim.o.swapfile=false
vim.cmd('edit tsu/t.h')
vim.bo.filetype='c' vim.treesitter.start(0,'c')
vim.wo.foldmethod='expr' vim.wo.foldexpr='v:lua.vim.treesitter.foldexpr()' vim.wo.foldlevel=99
local function dump(t) local s={} for l=1,vim.fn.line('$') do s[#s+1]=vim.fn.foldclosed(l)..'/'..vim.fn.foldclosedend(l) end print(t, vim.fn.line('.'), table.concat(s,' ')) end
local keys = {...}
local steps = vim.split(os.getenv('KEYS'), ' ')
local i = 0
local function step()
  i = i + 1
  if i > #steps then vim.cmd('qa!') return end
  vim.api.nvim_input(steps[i])
  vim.defer_fn(function() dump(steps[i]) vim.defer_fn(function() dump(steps[i]..'+later') step() end, 200) end, 0)
end
vim.defer_fn(function() dump('start') step() end, 300)
