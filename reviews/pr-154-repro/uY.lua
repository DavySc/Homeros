-- The oracle for tests/test_fold.c: random buffers and edits in Neovim with
-- foldmethod=expr and a foldexpr that gives ">n" and "n" as the tree-sitter,
-- Markdown and Org foldexprs do: a line of n stars starts a fold at level
-- n, a digit d is level d, y is "-1" (undefined: a neighbour's level, as
-- vim.treesitter.foldexpr() answers for a line added since the last
-- parse), Y (INC only) is ">-1" (_fold.lua's incremental order can give
-- it, PR 121), anything else has the level of the nearest star line above
-- it (0 if none).  Each scenario starts at some
-- 'foldlevel', then mixes nvim_buf_set_lines(), o, O, A<CR> (Insert mode)
-- and the fold commands; after each step it prints foldclosed(),
-- foldclosedend(), foldlevel() and the closed fold's level of every line.
-- Neovim caches whether a fold is 'foldminlines' or smaller (fd_small) and
-- some edits leave that stale, depending on when it last drew; the fold
-- module asks each time.  So scenarios with edits run with foldminlines 0
-- (no fold is small, test_fold.c says every line is tall), and those with
-- 'foldminlines' 1 have fold commands only, where the cache stays right.
-- usage: nvim --headless --clean -l tests/test_fold.lua N SEED [ONLY [inc]] > trace
-- (ONLY '' for none)
-- prints N scenarios, or with ONLY only the one numbered ONLY (from 0; run
-- N = ONLY + 1, so that Neovim gets to it in the same state).  The recorded
-- tests/test_fold.txt is N 300 SEED 1, then these scenarios (SEED:ONLY),
-- which reach the merges and truncations seed 1 does not (the review of
-- PR 107 found them by deleting those lines): 2:64 37:96 60:106 11:239
-- 71:5, then N 300 SEED 1 INC (syntax_foldexpr's incremental flow: ops T
-- and U, every line's value explicit)
-- Trace lines (lines from 0, a token per buffer line, '.' an empty one):
--   S level fml tok...        a new buffer, 'foldlevel', 'foldminlines'
--   E a b tok...              nvim_buf_set_lines(0, a, b, true, toks)
--   o|O|A line tok            o / O / A<CR> at line, then tok typed, <Esc>
--   Z cmd line count          :normal! count..cmd at line ('L': 'foldlevel' = count)
--   T a b k                   (INC) lines a to b replaced by k lines with
--                             nvim_buf_set_text(); a and the lines after it
--                             keep their foldexpr values, added lines are "-1"
--                             and the last ones go (_fold.lua's on_bytes)
--   U r1 r2 tok...            (INC) a parse gave lines [r1, r2) new values,
--                             then vim._foldupdate(r1, r2) (on_changedtree)
--   R rc lvl[/fc/fce/fl] ...  after it: rc -1 for "E490: No fold found",
--                             foldlevel() and, in a closed fold, foldclosed(),
--                             foldclosedend() and that fold's level
local N, seed, only = tonumber(arg[1] or 300), tonumber(arg[2] or 1), tonumber(arg[3])
-- INC: syntax_foldexpr's incremental flow, FE answering from V
local inc = arg[4] == 'inc'
math.randomseed(seed)
local V = {}

local function val(t)
  return t:match('^%*+$') and '>' .. #t or t == 'y' and '-1' or t == 'Y' and '>-1' or t
end

function _G.FE()
  if inc then return V[vim.v.lnum] end
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

local toks = { '*', '*', '*', '**', '**', '***', '****', '0', '1', '2', '3', 'x', 'x', 'x', 'x', 'x', 'y', '.' }
local itoks = { '*', '*', '**', '***', '0', '1', '2', '3', 'y', 'Y' }
local function tok()
  return inc and itoks[math.random(#itoks)] or toks[math.random(#toks)]
end
local function text(t) return t == '.' and '' or t end
local out, null = io.stdout, { write = function() end }

local function result(rc)
  local r = { 'R', tostring(rc) }
  for l = 1, vim.api.nvim_buf_line_count(0) do
    local a = vim.fn.foldclosed(l)
    r[#r + 1] = vim.fn.foldlevel(l) .. (a < 0 and '' or '/' .. a .. '/' .. vim.fn.foldclosedend(l)
      .. '/' .. vim.fn.foldtextresult(l))
  end
  out:write(table.concat(r, ' '), '\n')
end

local cmds = { 'za', 'za', 'za', 'zc', 'zc', 'zo', 'zo', 'zO', 'zR', 'zM', 'zv', 'zX', 'L' }
for s = 0, N - 1 do
  out = (not only or s == only) and io.stdout or null
  vim.cmd('enew!')
  vim.wo.foldmethod = 'expr'
  vim.wo.foldexpr = 'v:lua.FE()'
  vim.wo.wrap = false
  vim.wo.foldtext = 'v:foldlevel'   -- the closed fold's level
  local lvl, fml = ({ 0, 1, 2, 3, 99 })[math.random(5)], math.random(10) <= 3 and 1 or 0
  if inc then fml = 0 end
  vim.wo.foldlevel = lvl
  vim.wo.foldminlines = fml
  local t = { 'S', tostring(lvl), tostring(fml) }
  local lines = {}
  for i = 1, math.random(1, 25) do
    local k = tok()
    t[#t + 1] = k
    lines[i] = text(k)
    V[i] = val(k)
  end
  for i = #lines + 1, #V do V[i] = nil end
  vim.api.nvim_buf_set_lines(0, 0, -1, true, lines)
  out:write(table.concat(t, ' '), '\n')
  result(0)
  for _ = 1, 40 do
    local n = vim.api.nvim_buf_line_count(0)
    local r, rc = math.random(100), 0
    if fml == 1 then r = 100 end
    if inc and r <= 35 then
      local a = math.random(0, n - 1)
      local b = math.min(n - 1, a + math.random(0, 2))
      local k, ts = math.random(1, 4), {}
      for i = 1, k do ts[i] = 'x' end
      for _ = 1, k - (b - a + 1) do table.insert(V, b + 2, '-1') end
      for _ = 1, b - a + 1 - k do table.remove(V, a + k + 1) end
      vim.api.nvim_buf_set_text(0, a, 0, b, #vim.fn.getline(b + 1), ts)
      out:write('T ', a, ' ', b, ' ', k, '\n')
    elseif inc and r <= 60 then
      local r1 = math.random(0, n - 1)
      local r2 = math.min(n, r1 + math.random(1, 6))
      local e = { 'U', r1, r2 }
      for i = r1 + 1, r2 do
        local x = itoks[math.random(#itoks)]
        V[i] = val(x)
        e[#e + 1] = x
      end
      vim._foldupdate(vim.api.nvim_get_current_win(), r1, r2)
      out:write(table.concat(e, ' '), '\n')
    elseif r <= 35 then
      local a = math.random(0, n)
      local b = math.min(n, a + math.random(0, 3))
      local k = math.random(0, 4)
      if n - (b - a) + k < 1 then k = 1 end
      local e, ts = { 'E', tostring(a), tostring(b) }, {}
      for i = 1, k do
        local x = tok()
        e[#e + 1] = x
        ts[i] = text(x)
      end
      vim.api.nvim_buf_set_lines(0, a, b, true, ts)
      out:write(table.concat(e, ' '), '\n')
    elseif r <= 55 then
      local c, x = math.random(0, n - 1), tok()
      local op = ({ 'o', 'O', 'A' })[math.random(3)]
      vim.api.nvim_win_set_cursor(0, { c + 1, 0 })
      local keys = (op == 'A' and 'A\r' or op) .. text(x) .. '\27'
      vim.api.nvim_feedkeys(keys, 'nx', false)
      out:write(op, ' ', c, ' ', x, '\n')
    else
      local c, cmd, count = math.random(0, n - 1), cmds[math.random(#cmds)], math.random(1, 3)
      vim.api.nvim_win_set_cursor(0, { c + 1, 0 })
      if cmd == 'L' then
        count = ({ 0, 1, 2, 3, 99 })[math.random(5)]
        vim.wo.foldlevel = count
      else
        local ok = pcall(vim.cmd, 'normal! ' .. count .. cmd)
        rc = ok and 0 or -1
      end
      out:write('Z ', cmd, ' ', c, ' ', count, '\n')
    end
    result(rc)
  end
end
