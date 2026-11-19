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
vim.fn.writefile({ 'abc', '\tx', '漢字 y' }, 'f0')
vim.cmd('e f0')
for _, c in ipairs({ 120, 80, 40 }) do
  vim.o.columns = c
  show('width ' .. c)
end
vim.o.columns = 80
vim.cmd('vsplit')
for _, c in ipairs({ 30, 20, 16, 12, 10, 8, 6, 4, 3, 2, 1 }) do
  vim.api.nvim_win_set_width(0, c)
  show('width ' .. c)
end
show('other', vim.fn.win_getid(vim.fn.winnr('l')))
vim.cmd('only')
vim.api.nvim_win_set_cursor(0, { 2, 1 })
show('2,1')
vim.api.nvim_win_set_cursor(0, { 3, 7 })
show('3,7')
vim.cmd('normal! x')
show('x')
for _, m in ipairs({ 'i', 'R', 'v', 'V', '\22', 'c' }) do
  mode = m
  show('mode ' .. (m == '\22' and 'CTRL-V' or m))
end
mode = nil
vim.cmd('w | e new')
show('new')
vim.cmd('enew')
show('noname')
vim.fn.writefile(vim.fn['repeat']({ '123456789' }, 200), 'k1')
vim.cmd('e k1')
vim.api.nvim_win_set_cursor(0, { 100, 0 })
show('k1 100')
vim.api.nvim_win_set_cursor(0, { 2, 0 })
show('k1 2')
vim.api.nvim_win_set_cursor(0, { 58, 0 })
show('k1 58')
vim.fn.writefile({ 'e\u{301}e\u{301}x' }, 'c1')
vim.cmd('e c1')
vim.api.nvim_win_set_cursor(0, { 1, 6 })
show('c1')
vim.fn.writefile({}, 'e0')
vim.cmd('e e0')
show('e0')
