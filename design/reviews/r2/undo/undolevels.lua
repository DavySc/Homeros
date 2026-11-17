-- Neovim does not keep every state: with 'undolevels' = 3, six edits leave only the newest states reachable.
vim.o.swapfile = false; vim.o.undolevels = 3
local function k(keys) vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), 'xt', false) end
for i = 1, 6 do k(':put =' .. i .. '<CR>') end
local function count(e) local n = 0; for _, x in ipairs(e) do n = n + 1; if x.alt then n = n + count(x.alt) end end; return n end
io.stdout:write('default undolevels: ' .. vim.go.undolevels .. '... set to 3 for this run\n')
io.stdout:write('after 6 edits: states kept in the tree = ' .. count(vim.fn.undotree().entries) .. '\n')
local ok, err = pcall(vim.cmd, 'undo 1'); io.stdout:write(':undo 1 -> ' .. (ok and ('seq_cur ' .. vim.fn.undotree().seq_cur) or err) .. '\n')
vim.cmd('qa!')
