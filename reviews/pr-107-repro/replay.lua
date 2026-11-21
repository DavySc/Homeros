-- Re-record a trace's steps in Neovim: reads the S/E/o/O/A/Z lines of the
-- trace named by arg[1] (R lines ignored), runs them as tests/test_fold.lua
-- does, prints the trace with fresh R lines.
-- usage: nvim --headless --clean -l replay.lua steps.txt > trace.txt
function _G.FE()
  local l = vim.fn.getline(vim.v.lnum)
  if l:match('^%*+$') then return '>' .. #l end
  if l:match('^%d$') then return l end
  for i = vim.v.lnum - 1, 1, -1 do
    local s = vim.fn.getline(i)
    if s:match('^%*+$') then return tostring(#s) end
  end
  return '0'
end
local function text(t) return t == '.' and '' or t end
local out = io.stdout
local function result(rc)
  local r = { 'R', tostring(rc) }
  for l = 1, vim.api.nvim_buf_line_count(0) do
    local a = vim.fn.foldclosed(l)
    r[#r + 1] = vim.fn.foldlevel(l) .. (a < 0 and '' or '/' .. a .. '/' .. vim.fn.foldclosedend(l))
  end
  out:write(table.concat(r, ' '), '\n')
end
for ln in io.lines(arg[1]) do
  local w = vim.split(ln, ' ', { trimempty = true })
  local rc = 0
  if w[1] == 'S' then
    vim.cmd('enew!')
    vim.wo.foldmethod = 'expr'
    vim.wo.foldexpr = 'v:lua.FE()'
    vim.wo.wrap = false
    vim.wo.foldlevel = tonumber(w[2])
    vim.wo.foldminlines = tonumber(w[3])
    local lines = {}
    for i = 4, #w do lines[#lines + 1] = text(w[i]) end
    vim.api.nvim_buf_set_lines(0, 0, -1, true, lines)
  elseif w[1] == 'E' then
    local ts = {}
    for i = 4, #w do ts[#ts + 1] = text(w[i]) end
    vim.api.nvim_buf_set_lines(0, tonumber(w[2]), tonumber(w[3]), true, ts)
  elseif w[1] == 'Z' then
    vim.api.nvim_win_set_cursor(0, { tonumber(w[3]) + 1, 0 })
    if w[2] == 'L' then vim.wo.foldlevel = tonumber(w[4])
    else rc = pcall(vim.cmd, 'normal! ' .. w[4] .. w[2]) and 0 or -1 end
  elseif w[1] ~= 'R' then
    vim.api.nvim_win_set_cursor(0, { tonumber(w[2]) + 1, 0 })
    vim.api.nvim_feedkeys((w[1] == 'A' and 'A\r' or w[1]) .. text(w[3]) .. '\27', 'nx', false)
  end
  if w[1] ~= 'R' then out:write(ln, '\n'); result(rc) end
end
