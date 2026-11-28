-- the same hover K times in one C-only session: each float's height, by how the last one closed
local dir = vim.fn.tempname(); vim.fn.mkdir(dir)
vim.fn.writefile({'int x;', 'int y;', 'int foo(int x);'}, dir .. '/m.c')
vim.o.swapfile = false; vim.o.hidden = true
vim.cmd('edit ' .. dir .. '/m.c')
local out = {}
local id = vim.lsp.start({ name = 'fake', cmd = { 'python3', vim.env.FAKE, dir .. '/log' }, root_dir = dir })
vim.wait(5000, function() local c = vim.lsp.get_client_by_id(id) return c and c.initialized end)
local function fl()
  for _, w in ipairs(vim.api.nvim_list_wins()) do if vim.api.nvim_win_get_config(w).relative ~= '' then return w end end
end
local function settle(ms) vim.wait(ms or 1500, function() return false end) end
for i = 1, 6 do
  vim.api.nvim_win_set_cursor(0, {3, 4}); vim.lsp.buf.hover()
  vim.wait(3000, function() return fl() ~= nil end)
  local h0 = vim.api.nvim_win_get_height(fl())
  settle(); vim.cmd('redraw')
  out[#out + 1] = string.format('K %d (%s): height at open %d, after 1.5 s %d', i, vim.env.CLOSE, h0, vim.api.nvim_win_get_height(fl()))
  if i == 1 or i == 6 then
    local g = {}
    for r = 1, 16 do local s = '' for c = 1, 40 do s = s .. vim.fn.screenstring(r, c) end g[#g + 1] = '   |' .. s .. '|' end
    vim.list_extend(out, g)
  end
  if vim.env.CLOSE == 'api' then vim.api.nvim_win_close(fl(), true) else vim.api.nvim_win_set_cursor(0, {3, 5}) end
  settle(300)
  if i == 3 then vim.cmd('edit ' .. dir .. '/n.md'); vim.cmd('redraw'); settle(300); vim.cmd('edit ' .. dir .. '/m.c'); out[#out + 1] = '(a Markdown buffer opened and left)' end
end
vim.fn.writefile(out, vim.env.OUT)
vim.cmd('qa!')
