-- tests/test_keys_ex.c's cases in Neovim: nvim --headless --clean -l
-- src/keys/ex_oracle.lua TEXT CASES OUT I.  Line I of CASES is typed into a
-- buffer holding TEXT's lines, with Davy's 'ignorecase' and 'smartcase'
-- (FEATURES.md 1); OUT gets "line col error", then the buffer's lines
-- joined by \1 (a NUL in a line, from \n in a :s replacement, as \2),
-- then the top line and the 'hlsearch' cells of each line shown
-- (# Search or CurSearch, . not; the line break's cell last), by \1.
-- One case a process: Neovim keeps the last search, redo and Visual area
-- from one buffer to the next, and some cases loop after others (not
-- traced).
local text = vim.split(io.open(_G.arg[1]):read('*a'), '\n')
table.remove(text)
local i, k = tonumber(_G.arg[4]), nil
for l in io.lines(_G.arg[2]) do
  i = i - 1
  if i == 0 then k = l break end
end
-- a command line that writes (:x file) writes there, not in the repository
vim.fn.chdir(vim.fs.dirname(_G.arg[3]))
vim.o.ignorecase = true
vim.o.smartcase = true
vim.o.scrolloff = 6   -- FEATURES.md's, which the core keeps (zt)
-- FEATURES.md's indent, as tools/conform.py sets it (o on a tab-indented line)
vim.o.expandtab, vim.o.shiftwidth, vim.o.softtabstop = true, 4, 4
-- 'nowrap' (FEATURES.md): a line's cells past the window's 80 are not
-- shown, and a view scrolled sideways is not compared ('?')
vim.o.wrap = false
-- with typeahead (feedkeys) a :s range's incsearch preview limits
-- 'hlsearch' to that range after the command (search_first_line is not
-- reset when the preview never drew, measured); typed, it is not
vim.o.incsearch = false
-- with no undo step, as a file read (u undoes only what the keys did)
vim.bo.undolevels = -1
vim.api.nvim_buf_set_lines(0, 0, -1, false, text)
vim.bo.undolevels = 1000
-- test_keys_ex.c's fold phase: $FOLDS is the foldexpr's value a line,
-- with Davy's 'foldlevel' 99 (FEATURES.md)
local folds = os.getenv('FOLDS')
if folds and folds ~= '' then
  _G.FX = vim.split(folds, ',')
  vim.o.foldlevel = 99
  vim.wo.foldmethod = 'expr'
  vim.wo.foldexpr = "luaeval('FX[_A]', v:lnum)"
end
vim.cmd('clearjumps')   -- -l leaves a jump on line 1 (measured: jjw CTRL-O went there)
local function finish()
  -- a cell is a match's when 'hlsearch' changes how it is drawn
  local function screen()
    vim.cmd('silent! redraw!')   -- silent: no second's pause for an error shown (msg_check_for_delay())
    local r = {}
    for l = vim.fn.line('w0'), vim.fn.line('w$') do
      for c = 1, math.min(vim.fn.strdisplaywidth(vim.fn.getline(l)) + 1, vim.fn.winwidth(0)) do r[#r + 1] = vim.fn.screenattr(l - vim.fn.line('w0') + 1, c) end
      r[#r + 1] = '|'
    end
    return r
  end
  local h = vim.v.hlsearch   -- 0 after :noh
  vim.v.hlsearch = 0
  local plain = screen()
  vim.v.hlsearch = h
  local hl, top = '', vim.fn.line('w0')
  for i, a in ipairs(screen()) do hl = hl .. (a == '|' and '\1' or a ~= plain[i] and '#' or '.') end
  if vim.fn.winsaveview().leftcol > 0 or vim.wo.foldmethod == 'expr' then hl = '?\1' end   -- folds: rows are not lines
  io.open(_G.arg[3], 'w'):write(string.format('%d %d %s\n%s\n%d\1%s\n', vim.fn.line('.'), vim.fn.col('.') - 1,
                                vim.v.errmsg:match('^E%d+') or '-', (table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '\1'):gsub('[%z\n]', '\2')),
                                top - 1, hl:sub(1, -2)))
  vim.cmd('qa!')
end
if not _G.FX then
  pcall(vim.api.nvim_feedkeys, k, 'xt', false)
  finish()
else
  -- typed, as Davy's keys are: only then does normal_check() run
  -- foldAdjustVisual() between commands (feedkeys() 'x' runs exec_normal(),
  -- which skips it; measured: 3GzaVggy yanked 2 lines, typed 7); the main
  -- loop reads them, so this runs from -c, not -l (test_keys_ex.c)
  vim.keymap.set({ 'n', 'x', 'o', 'i', 'c', 's' }, '<F35>', finish)
  vim.api.nvim_create_autocmd('VimEnter', { callback = function()
    -- startup's jump; its setpcmark() (main.c, which -l never reaches)
    -- stays: test_keys_ex.c's fold cases start with m'
    vim.cmd('clearjumps')
    vim.api.nvim_input(k:gsub('<', '<lt>') .. '<F35>')
  end })
end
