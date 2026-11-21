-- The oracle for tests/test_core_status.c: lualine with catppuccin's
-- lualine theme, set up as UI proposal 5's mockup (rounded mode and
-- progress pills; branch, file name, diagnostics on the left; LSP, file
-- type, encoding, location, progress on the right), laid out by
-- nvim_eval_statusline().  Prints one line per state: the tag, the text,
-- then each highlight's start byte and group.
-- usage, in an empty directory with HOME=$PWD:
--   LUALINE=<lualine.nvim> CATPPUCCIN=<catppuccin/nvim> \
--   nvim --headless --clean -c 'luafile test_core_status.lua' -c 'qa!' > test_core_status.txt
vim.opt.rtp:prepend(os.getenv('CATPPUCCIN'))
vim.opt.rtp:prepend(os.getenv('LUALINE'))
vim.o.termguicolors = true
require('catppuccin').setup({ flavour = 'mocha' })
vim.cmd.colorscheme('catppuccin')
local pill = { left = '\u{e0b6}', right = '\u{e0b4}' }
require('lualine').setup({
  options = { theme = 'catppuccin-mocha', component_separators = { left = '', right = '\u{b7}' },
              section_separators = { left = '\u{e0b4}', right = '\u{e0b6}' } },
  sections = {
    lualine_a = { { 'mode', separator = pill } },
    lualine_b = {},
    lualine_c = { { 'branch', icon = '\u{2387}' }, { 'filename', symbols = { modified = '\u{25cf}' } },
                  { 'diagnostics', symbols = { error = '\u{2716} ', warn = '\u{25b2} ', info = '\u{25c6} ', hint = '\u{25c7} ' } } },
    lualine_x = { { 'lsp_status', icon = '' }, { 'filetype', icons_enabled = false }, 'encoding' },
    lualine_y = { 'location' },
    lualine_z = { { 'progress', separator = pill } },
  },
  inactive_sections = { lualine_c = { { 'filename', symbols = { modified = '\u{25cf}' } } }, lualine_x = { 'location' } },
})
vim.o.laststatus = 2
-- the modes, as lualine reads them, without entering them
local mode
local get_mode = vim.api.nvim_get_mode
vim.api.nvim_get_mode = function() return mode and { mode = mode, blocking = false } or get_mode() end
local fmode = vim.fn.mode
vim.fn.mode = function(...) return mode or fmode(...) end
local function show(tag, w)
  w = w or vim.api.nvim_get_current_win()
  require('lualine').refresh({ force = true, scope = 'tabpage', place = { 'statusline' } })
  local r = vim.api.nvim_eval_statusline(vim.wo[w].statusline, { winid = w, highlights = true, maxwidth = vim.api.nvim_win_get_width(w) })
  local hl = {}
  for _, h in ipairs(r.highlights) do hl[#hl + 1] = h.start .. ':' .. h.group end
  io.stdout:write(tag .. '\t' .. r.str .. '\t' .. table.concat(hl, ' ') .. '\n')
end
vim.o.columns = 80
vim.fn.system({ 'git', 'init', '-q', '-b', 'feature/長い-branch' })
vim.fn.writefile({ 'select 1' }, '漢字ファイル名.sql')
vim.cmd('e 漢字ファイル名.sql')
vim.cmd('normal! x')
vim.cmd('vsplit')
local cur, other = vim.api.nvim_get_current_win(), vim.fn.win_getid(vim.fn.winnr('l'))
for c = 1, 70 do
  vim.api.nvim_win_set_width(cur, c)
  show('w' .. c)
end
for c = 1, 25 do
  vim.api.nvim_win_set_width(cur, 79 - c)
  show('o' .. c, other)
end
vim.cmd('only')
-- a new file after its first :w
vim.cmd('w | e new2')
show('new-git')
vim.cmd('normal! ix')
vim.cmd('w')
show('saved-new')
mode = 'no'
show('mode no')
mode = nil
vim.fn.writefile(vim.fn['repeat']({ 'a' }, 12345), 'big')
vim.cmd('e big')
vim.api.nvim_win_set_cursor(0, { 1234, 0 })
show('big')
