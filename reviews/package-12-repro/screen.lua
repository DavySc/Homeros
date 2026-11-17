-- Review of package 12: what Neovim's own highlighter draws.  Opens FILE,
-- scrolls line L0 (0-based) to the top of a window of N lines, and lets
-- Neovim redraw for real; every extmark highlighter.lua sets during the
-- redraw is recorded (nvim_buf_set_extmark is wrapped).  Per byte the mark
-- with the highest priority wins, the later one on a tie (decor.c).  Prints
-- one face char per byte of the window's lines, as dump.c does.
-- (Reading the cells with nvim__inspect_cell was tried first: without a UI
-- the attribute ids it returns were stale, measured 3 runs, 3 different.)
-- usage: nvim --headless --clean -l screen.lua FILE L0 N
local groups = {}
do  -- names[] of src/syntax/syntax.c: name i is face i + 1
  local src = io.open('src/syntax/syntax.c'):read('*a')
  local i = 0
  for name in src:match('names%[%] = {(.-)};'):gmatch('"([%w_.]+)"') do i = i + 1; groups[name] = i end
  assert(i == 90)
end
local function face(cap)
  if cap:sub(1, 1) == '_' then return 0 end
  while cap ~= '' do
    if groups[cap] then return groups[cap] end
    cap = cap:match('^(.*)%.[^.]*$') or ''
  end
  return 0
end
local file, l0, n = arg[1], tonumber(arg[2]), tonumber(arg[3])
-- the headless window stays 80 wide: wrap, so whole lines are drawn
-- and 22 high (a taller one crashed nvim, measured): few lines per window
assert(n <= 10)
vim.o.wrap, vim.o.laststatus, vim.o.foldenable = true, 0, false
vim.cmd('edit ++ff=unix ' .. vim.fn.fnameescape(file))
local buf = vim.api.nvim_get_current_buf()
vim.treesitter.start(buf)
vim.fn.winrestview({ topline = l0 + 1, lnum = l0 + 1, col = 0, leftcol = 0 })
local marks
local real = vim.api.nvim_buf_set_extmark
vim.api.nvim_buf_set_extmark = function(b, ns, row, col, o)
  if marks and b == buf and o.ephemeral and o.hl_group then
    marks[#marks + 1] = { row, col, o.end_line or o.end_row or row, o.end_col or col, o.hl_group, o.priority or 4096 }
  end
  return real(b, ns, row, col, o)
end
vim.api.nvim__redraw({ flush = true, valid = false })
vim.wait(300)   -- the async parse and its redraw
marks = {}
vim.api.nvim__redraw({ flush = true, valid = false })
assert(vim.fn.line('w0') - 1 == l0, 'topline')
assert(#marks > 0, 'no marks: the highlighter did not draw')
local lines = vim.api.nvim_buf_get_lines(buf, l0, l0 + n, false)
local f, pr = {}, {}
for r = 1, #lines do f[r], pr[r] = {}, {} end
for _, m in ipairs(marks) do
  local name = type(m[5]) == 'number' and vim.fn.synIDattr(m[5], 'name') or m[5]
  local lang = vim.treesitter.language.get_lang(vim.bo[buf].filetype)
  local fc = face((name:gsub('^@', ''):gsub('%.[%w_]+$', '')))
  if fc > 0 then
    for row = math.max(m[1], l0), math.min(m[3], l0 + #lines - 1) do
      local r = row - l0 + 1
      local a = row == m[1] and m[2] or 0
      local z = row == m[3] and m[4] or #lines[r] + 1
      for c = a + 1, math.min(z, #lines[r]) do
        if m[6] >= (pr[r][c] or -1) then f[r][c], pr[r][c] = fc, m[6] end
      end
    end
  end
end
local out = {}
for r, line in ipairs(lines) do
  local o = {}
  for c = 1, #line do o[c] = string.char(48 + (f[r][c] or 0)) end
  -- a line that did not fit in the window was not drawn
  out[#out + 1] = l0 + r <= vim.fn.line('w$') and table.concat(o) or 'SKIP'
end
io.stdout:write(table.concat(out, '\n'), '\n')
