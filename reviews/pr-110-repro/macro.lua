-- Review of PR 110: the captures Neovim draws on line 1 of FILE, without
-- and with nvim-treesitter's plugin/filetypes.lua line "systemverilog =
-- { 'verilog' }" (vim.treesitter.language.register), which is what makes a
-- verilog buffer use the systemverilog parser in the first place.
-- nvim --headless --clean -l macro.lua FILE [register]   (from the repo root)
vim.opt.rtp:prepend('build/syntax/src/nvim')
if arg[2] then vim.treesitter.language.register('systemverilog', { 'verilog' }) end
vim.cmd.edit(arg[1])
vim.treesitter.start(0, 'systemverilog')
vim.treesitter.get_parser():parse(true)
local line = vim.api.nvim_buf_get_lines(0, 0, 1, false)[1]
local o = {}
for c = 0, #line - 1 do
  local caps = vim.treesitter.get_captures_at_pos(0, 0, c)
  local top = caps[#caps]
  o[#o + 1] = top and top.capture:sub(1, 1) ~= '_' and top.capture ~= 'spell' and top.capture:sub(1, 1) or '.'
end
io.write(line, '\n', table.concat(o), '\n')
local n = 0
vim.treesitter.get_parser():for_each_tree(function() n = n + 1 end)
io.write(n, ' trees\n')
