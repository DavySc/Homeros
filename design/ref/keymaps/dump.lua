local out = {}
local function add(scope, m)
  local rhs = m.rhs or (m.callback and 'lua') or ''
  table.insert(out, string.format('%s\t%s\t%s\t%s', scope, m.mode, m.lhs:gsub(' ', '<Space>'), (m.desc or rhs):gsub('\n',' ')))
end
for _, mode in ipairs({'n','x','o','i','c','t'}) do for _, m in ipairs(vim.api.nvim_get_keymap(mode)) do add('global', m) end end
local function bufdump(ft)
  vim.cmd('enew'); vim.bo.filetype = ft
  for _, mode in ipairs({'n','x','o','i'}) do for _, m in ipairs(vim.api.nvim_buf_get_keymap(0, mode)) do add('ft:'..ft, m) end end
end
for _, ft in ipairs({'lean','org','sql','c','markdown'}) do pcall(bufdump, ft) end
local f = io.open('keymaps.tsv', 'w'); f:write(table.concat(out, '\n')); f:close()
vim.cmd('qa!')
