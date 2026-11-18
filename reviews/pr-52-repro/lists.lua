-- Review of PR 52: what Neovim does to the change list, the jump list and
-- the marks a . ^ when d5w deletes from the middle of line 1 into line 4.
-- nvim --clean --headless -c 'set noswapfile' -l lists.lua   (prints to stdout)
local function feed(k) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(k, true, true, true), 'xt', false) end
local function show(tag)
  local cl = vim.fn.getchangelist()[1]
  local jl = vim.fn.getjumplist()[1]
  local s = tag .. ' changes:'
  for _, c in ipairs(cl) do s = s .. (' (%d,%d)'):format(c.lnum, c.col) end
  s = s .. ' jumps:'
  for _, j in ipairs(jl) do s = s .. (' (%d,%d)'):format(j.lnum, j.col) end
  for _, m in ipairs({ "'a", "'b", "'c", "'.", "'^", "'<" }) do
    local p = vim.fn.getpos(m)
    s = s .. (' %s=(%d,%d)'):format(m, p[2], p[3] - 1)
  end
  print(s)
end
vim.api.nvim_buf_set_lines(0, 0, -1, false, { 'abc def', 'xyz uvw', 'foo bar', 'bar baz', 'qq' })
vim.cmd('clearjumps')
-- a change on line 2 col 5 and on line 3 col 2 and line 4 col 1; marks a b c
-- on lines 2 3 4; jumps from 2,5 and 3,2 and 4,1; '< on 3,2
feed("2G5|rUma3G3|rOmbv<Esc>4G2|rAmc")
feed("2G5|m'3G3|m'4G2|m'ggfdh")
show('before')
feed('ld5w')
show('after ')
feed('u')
show('undone')
feed('<C-r>')
show('redone')
vim.cmd('qa!')
