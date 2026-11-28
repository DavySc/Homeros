-- PR 165 review: what Neovim 0.12 does with the keys on answers from an
-- in-process server.  nvim --clean --headless -c 'set noswapfile' -c 'luafile nvim_keys.lua' m.c
local dir = vim.fn.getcwd()
local uri = function(n) return vim.uri_from_fname(dir .. '/' .. n) end
local R = function(n, l, c) return { uri = uri(n), range = { start = { line = l, character = c }, ['end'] = { line = l, character = c + 1 } } } end
local answer = {}
local server = function()
  return {
    request = function(method, params, cb)
      if method == 'initialize' then cb(nil, { capabilities = { definitionProvider = true, referencesProvider = true, documentSymbolProvider = true, implementationProvider = true, typeDefinitionProvider = true } })
      else cb(nil, answer[method]) end
      return true, 1
    end,
    notify = function() return true end, is_closing = function() return false end, terminate = function() end,
  }
end
vim.lsp.start({ name = 'fake', cmd = server, root_dir = dir })
vim.wait(200)
local out = {}
local function say(what)
  local p = vim.api.nvim_win_get_cursor(0)
  out[#out + 1] = string.format('%-34s %s %d,%d win %d/%d bt=%s msg=%s', what, vim.fn.expand('%:.'), p[1] - 1, p[2], vim.fn.winnr(), vim.fn.winnr('$'), vim.bo.buftype,
    vim.fn.execute('messages'):gsub('\n', ' | '))
  vim.cmd('messages clear')
end
local function keys(k) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(k, true, false, true), 'x', false); vim.wait(100) end
-- C-] with two definitions, the later one first
answer['textDocument/definition'] = { R('m.c', 3, 4), R('m.c', 1, 4) }
keys('<C-]>'); say('C-] two defs (3,4 then 1,4)')
keys('<C-o>'); say('C-o')
answer['textDocument/definition'] = { R('z.h', 0, 4), R('m.c', 2, 4) }
keys('<C-]>'); say('C-] z.h then m.c')
keys('<C-o>'); say('C-o')
answer['textDocument/definition'] = {}
keys('<C-]>'); say('C-] none')
answer['textDocument/references'] = { R('m.c', 1, 4), R('m.c', 2, 4), R('z.h', 0, 4) }
keys('grr'); say('grr three'); out[#out + 1] = table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), ' || ')
keys('<CR>'); say('Enter')
vim.cmd('cclose')
answer['textDocument/references'] = {}
keys('grr'); say('grr none')
answer['textDocument/implementation'] = { R('m.c', 3, 4), R('m.c', 1, 4) }
keys('gri'); say('gri two'); vim.cmd('cclose')
answer['textDocument/implementation'] = { R('m.c', 3, 4) }
keys('gri'); say('gri one')
keys('<C-o>'); say('C-o')
answer['textDocument/implementation'] = {}
keys('gri'); say('gri none')
answer['textDocument/documentSymbol'] = {}
keys('gO'); say('gO none')
answer['textDocument/documentSymbol'] = { { name = 'a', kind = 13, range = R('m.c', 0, 4).range, selectionRange = R('m.c', 0, 4).range } }
keys('gO'); say('gO one')
local f = io.open('out.txt', 'w'); f:write(table.concat(out, '\n') .. '\n'); f:close()
vim.cmd('qa!')
