-- test_keys_clip.c's steps in Neovim with clipboard=unnamedplus and a fake
-- provider (g:clipboard); run: nvim --headless --clean -l src/keys/clip_oracle.lua
-- (measured 2026-11-18, 1 run: the same text and clipboard after each step)
local C = nil   -- {lines, regtype} as set; or plain lines for "external"
local function cp(lines, rt) C = {lines, rt} end
vim.g.clipboard = { name = 'fake', copy = { ['+'] = cp, ['*'] = cp },
  paste = { ['+'] = function() return C end, ['*'] = function() return C end }, cache_enabled = 0 }
vim.o.clipboard = 'unnamedplus'
vim.o.swapfile = false
local function run(text, steps)
  vim.cmd('enew!'); vim.api.nvim_buf_set_lines(0, 0, -1, false, text)
  for _, s in ipairs(steps) do
    if s.ext then C = s.ext end
    if s.k then vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(s.k, true, false, true), 'xt', false) end
    local cl = C and (type(C[1]) == 'table' and table.concat(C[1], '\\n') .. '|' .. C[2] or table.concat(C, '\\n')) or 'nil'
    print(s.k or '', vim.inspect(vim.api.nvim_buf_get_lines(0, 0, -1, false)), cl)
  end
end
run({'one two', 'three'}, {{k='yy'}, {k='dw'}, {k='"ayyj"_dd'}, {ext={'x','y',''}, k='p'}, {ext={'ab'}, k='gg0P'}, {k='"*yiw'}, {ext={'q'}, k='"+P'}})
run({'ab', 'cd'}, {{k='<C-v>jy'}, {k='$p'}})
run({'ab cd'}, {{ext={'XY'}, k='wviwp'}})
vim.cmd('qa!')
