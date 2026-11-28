-- PR 165 review: C-] to the cursor's own position, then C-o (seed 165's case 5)
local dir = vim.fn.getcwd()
local server = function()
  return { request = function(method, params, cb)
      if method == 'initialize' then cb(nil, { capabilities = { definitionProvider = true } })
      else cb(nil, { { uri = vim.uri_from_fname(dir .. '/m.c'), range = { start = { line = 0, character = 0 }, ['end'] = { line = 0, character = 1 } } } }) end
      return true, 1 end,
    notify = function() return true end, is_closing = function() return false end, terminate = function() end }
end
vim.lsp.start({ name = 'fake', cmd = server, root_dir = dir })
vim.wait(200)
local out = {}
local function keys(k, what) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(k, true, false, true), 'x', false); vim.wait(100)
  local p = vim.api.nvim_win_get_cursor(0); out[#out + 1] = string.format('%-10s %s %d,%d', what, vim.fn.expand('%:t'), p[1] - 1, p[2]) end
vim.cmd('edit z.h'); keys('<C-o>', 'C-o'); keys('<C-]>', 'C-]'); keys('<C-o>', 'C-o')
io.open('out.txt', 'w'):write(table.concat(out, '\n') .. '\n')
vim.cmd('qa!')
