function _G.FXC(l) local t = vim.fn.getline(l) if t == '' then return '-1' end local ind = #t:match('^ *') if t:sub(-1) == '{' then return '>' .. (ind + 1) end return tostring(ind) end
vim.o.foldlevel = 99
vim.wo.foldmethod = 'expr'
vim.wo.foldexpr = 'v:lua.FXC(v:lnum)'
function _G.FC() local t = {} for l = 1, vim.fn.line('$') do t[#t+1] = vim.fn.foldclosed(l) end return table.concat(t, ',') .. ' cur ' .. vim.fn.line('.') .. ' lv ' .. table.concat(vim.tbl_map(function(l) return vim.fn.foldlevel(l) end, vim.fn.range(1, vim.fn.line('$'))), ',') end
local out = io.open('out.txt', 'a')
vim.keymap.set({'n','i','x'}, '<F34>', function() out:write(_G.FC() .. '\n') out:flush() end)
vim.keymap.set({'n','i','x'}, '<F35>', function() out:write(_G.FC() .. ' END\n') out:close() vim.cmd('qa!') end)
vim.api.nvim_create_autocmd('VimEnter', { callback = function() vim.api.nvim_input(vim.env.K .. '<F35>') end })
