-- Neovim's side of the PR 180 probes.  usage (in a tmux 80x24, or headless):
-- FAKE=fake.py PROBE=p1|p2|p4|range nvim --clean [-c "luafile tests/test_core_markdown.lua"] -c "luafile nv.lua"
local dir = vim.fn.tempname(); vim.fn.mkdir(dir)
vim.fn.writefile({'int x;', 'int y;', 'int foo(int x);'}, dir .. '/m.c')
vim.fn.writefile({'char o;', 'char p;'}, dir .. '/o.c')
vim.o.swapfile = false; vim.o.hidden = true
vim.cmd('edit ' .. dir .. '/m.c')
local out = {}
local function say(s) out[#out + 1] = s end
local id = vim.lsp.start({ name = 'fake', cmd = { 'python3', vim.env.FAKE, dir .. '/log' }, root_dir = dir })
vim.wait(5000, function() local c = vim.lsp.get_client_by_id(id) return c and c.initialized end)
local function floats()
  local n = {}
  for _, w in ipairs(vim.api.nvim_list_wins()) do
    if vim.api.nvim_win_get_config(w).relative ~= '' then
      vim.cmd('redraw')
      local p = vim.api.nvim_win_get_position(w)
      n[#n + 1] = string.format('float y %d x %d w %d h %d: %s', p[1], p[2], vim.api.nvim_win_get_width(w), vim.api.nvim_win_get_height(w),
        vim.inspect(vim.api.nvim_buf_get_lines(vim.api.nvim_win_get_buf(w), 0, -1, false)))
    end
  end
  return #n > 0 and table.concat(n, '; ') or 'no float'
end
local function settle() vim.wait(1500, function() return false end) end
local P = vim.env.PROBE
if P == 'p1' then
  vim.api.nvim_win_set_cursor(0, {1, 4}); vim.lsp.buf.hover()
  vim.api.nvim_win_set_cursor(0, {2, 4}); vim.lsp.buf.hover()
  settle(); say(floats()); say('messages: ' .. vim.trim(vim.fn.execute('messages')))
elseif P == 'p2' then
  vim.api.nvim_win_set_cursor(0, {1, 0}); vim.lsp.buf.hover(); settle(); say(floats())
  vim.cmd('edit ' .. dir .. '/o.c'); settle(); say('after :e o.c: ' .. floats())
elseif P == 'p4' then
  vim.api.nvim_win_set_cursor(0, {3, 4}); vim.lsp.buf.hover(); settle(); say('first: ' .. floats())
  vim.api.nvim_win_set_cursor(0, {3, 5}); settle()
  vim.api.nvim_win_set_cursor(0, {3, 4}); vim.lsp.buf.hover(); settle(); say('second: ' .. floats())
elseif P == 'range' then
  vim.api.nvim_win_set_cursor(0, {1, 4}); vim.lsp.buf.hover(); settle(); say(floats())
  for name, ns in pairs(vim.api.nvim_get_namespaces()) do
    for _, m in ipairs(vim.api.nvim_buf_get_extmarks(0, ns, 0, -1, { details = true })) do
      say(string.format('extmark ns %s at %d,%d to %d,%d hl %s', name, m[2], m[3], m[4].end_row or -1, m[4].end_col or -1, tostring(m[4].hl_group)))
    end
  end
  say('LspReferenceTarget: ' .. vim.inspect(vim.api.nvim_get_hl(0, { name = 'LspReferenceTarget', link = false })))
end
say('server saw: ' .. table.concat(vim.fn.readfile(dir .. '/log'), ' | '))
vim.fn.writefile(out, vim.env.OUT)
vim.cmd('qa!')
