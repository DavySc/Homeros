-- Review of PR 12: Neovim as an editor (no FFI).  For each line of arg[1]:
-- the byte columns `l` visits from column 0 with their screen widths
-- (virtcol), then the columns `h` visits from the last one.
-- nvim --clean --headless -l editor.lua lines.bin > nvim.out
vim.o.swapfile = false
local f = io.open(arg[1], 'rb')
local data = f:read('*a'); f:close()
for line in data:gmatch('([^\n]*)\n') do
  if #line > 0 then
    vim.api.nvim_buf_set_lines(0, 0, -1, false, { line })
    vim.api.nvim_win_set_cursor(0, { 1, 0 })
    local out, cols = {}, {}
    while true do
      local c = vim.fn.col('.') - 1
      local v = vim.fn.virtcol({ 1, c + 1 }, 1)
      out[#out + 1] = c .. ':' .. (v[2] - v[1] + 1)
      cols[#cols + 1] = c
      vim.cmd('normal! l')
      if vim.fn.col('.') - 1 == c then break end
    end
    local back = {}
    while true do
      local c = vim.fn.col('.') - 1
      vim.cmd('normal! h')
      local d = vim.fn.col('.') - 1
      if d == c then break end
      back[#back + 1] = d
    end
    io.write(table.concat(out, ' '), ' | ', table.concat(back, ' '), '\n')
  else
    io.write('\n')
  end
end
