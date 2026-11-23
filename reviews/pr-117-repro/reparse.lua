-- Review probe for PR 117: the tree-sitter flow.  Levels live in a table T
-- (not the text), an edit adds rows as "y" (-1), then a "parse" gives them
-- real tokens and calls vim._foldupdate() on those rows, as _fold.lua does.
local N, seed = tonumber(arg[1] or 300), tonumber(arg[2] or 1)
math.randomseed(seed)
local T = {}
function _G.FE()
  local l = T[vim.v.lnum]
  if l:match('^%*+$') then return '>' .. #l end
  if l:match('^%d$') then return l end
  if l == 'y' then return -1 end
  for i = vim.v.lnum - 1, 1, -1 do
    if T[i]:match('^%*+$') then return tostring(#T[i]) end
  end
  return '0'
end
local toks = { '*', '*', '*', '**', '**', '***', '0', '1', '2', '3', 'x', 'x', 'x', 'x', '.' }
local function tok() return toks[math.random(#toks)] end
local out = io.stdout
local function result(rc)
  local r = { 'R', tostring(rc) }
  for l = 1, vim.api.nvim_buf_line_count(0) do
    local a = vim.fn.foldclosed(l)
    r[#r + 1] = vim.fn.foldlevel(l) .. (a < 0 and '' or '/' .. a .. '/' .. vim.fn.foldclosedend(l))
  end
  out:write(table.concat(r, ' '), '\n')
end
local cmds = { 'za', 'zc', 'zo', 'zO', 'zR', 'zM', 'zv', 'zX', 'L' }
for s = 0, N - 1 do
  vim.cmd('enew!')
  T = { 'x' }
  vim.wo.foldmethod = 'expr'
  vim.wo.foldexpr = 'v:lua.FE()'
  vim.wo.wrap = false
  vim.wo.foldminlines = 0
  local lvl = ({ 0, 1, 2, 3, 99 })[math.random(5)]
  vim.wo.foldlevel = lvl
  local t, lines = { 'S', tostring(lvl), '0' }, {}
  for i = 1, math.random(1, 25) do lines[i] = tok(); t[#t + 1] = lines[i] end
  T = vim.deepcopy(lines)
  vim.api.nvim_buf_set_lines(0, 0, -1, true, lines)
  out:write(table.concat(t, ' '), '\n')
  result(0)
  local pend = nil  -- {a, k}: rows waiting for the parse
  for _ = 1, 40 do
    local n = #T
    local r, rc = math.random(100), 0
    if pend and r <= 50 then
      -- the parse: rows a..a+k get levels, plus up to 2 neighbours change too
      local a, b = pend[1], pend[1] + pend[2]
      a = math.max(0, a - math.random(0, 2)); b = math.min(n, b + math.random(0, 2))
      local e = { 'U', tostring(a), tostring(b) }
      for i = a + 1, b do T[i] = tok(); e[#e + 1] = T[i] end
      -- any row still y outside the range would be a probe bug
      vim._foldupdate(vim.api.nvim_get_current_win(), a, b)
      out:write(table.concat(e, ' '), '\n')
      pend = nil
    elseif not pend and r <= 50 then
      -- an edit: delete d rows at a, add k rows there, all undefined
      local a = math.random(0, n)
      local b = math.min(n, a + math.random(0, 3))
      local k = math.random(1, 4)
      local e, ts = { 'E', tostring(a), tostring(b) }, {}
      for _ = a + 1, b do table.remove(T, a + 1) end
      for i = 1, k do table.insert(T, a + 1, 'y'); e[#e + 1] = 'y'; ts[i] = 'y' end
      vim.api.nvim_buf_set_lines(0, a, b, true, ts)
      out:write(table.concat(e, ' '), '\n')
      pend = { a, k }
    else
      local c, cmd, count = math.random(0, n - 1), cmds[math.random(#cmds)], math.random(1, 3)
      vim.api.nvim_win_set_cursor(0, { c + 1, 0 })
      if cmd == 'L' then
        count = ({ 0, 1, 2, 3, 99 })[math.random(5)]
        vim.wo.foldlevel = count
      else
        rc = pcall(vim.cmd, 'normal! ' .. count .. cmd) and 0 or -1
      end
      out:write('Z ', cmd, ' ', c, ' ', count, '\n')
    end
    result(rc)
  end
end
