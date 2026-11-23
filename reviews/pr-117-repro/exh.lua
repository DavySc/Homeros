-- every buffer of up to 5 lines over 6 tokens: the folds built fresh
function _G.FE()
  local l = vim.fn.getline(vim.v.lnum)
  if l:match('^%*+$') then return '>' .. #l end
  if l:match('^%d$') then return l end
  if l == 'y' then return '-1' end
  for i = vim.v.lnum - 1, 1, -1 do
    local s = vim.fn.getline(i)
    if s:match('^%*+$') then return tostring(#s) end
  end
  return '0'
end
local toks = { '*', '**', '1', '3', 'x', 'y' }
local function go(lines)
  vim.cmd('enew!')
  vim.wo.foldmethod = 'expr'; vim.wo.foldexpr = 'v:lua.FE()'; vim.wo.wrap = false
  vim.wo.foldlevel = #lines % 2; vim.wo.foldminlines = 0
  vim.api.nvim_buf_set_lines(0, 0, -1, true, lines)
  io.stdout:write('S ', #lines % 2, ' 0 ', table.concat(lines, ' '), '\n')
  local r = { 'R', '0' }
  for l = 1, #lines do
    local a = vim.fn.foldclosed(l)
    r[#r + 1] = vim.fn.foldlevel(l) .. (a < 0 and '' or '/' .. a .. '/' .. vim.fn.foldclosedend(l))
  end
  io.stdout:write(table.concat(r, ' '), '\n')
end
local function rec(lines, d)
  if #lines > 0 then go(lines) end
  if d == 0 then return end
  for _, t in ipairs(toks) do lines[#lines + 1] = t; rec(lines, d - 1); lines[#lines] = nil end
end
rec({}, 5)
