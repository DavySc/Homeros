-- Neovim 0.12 with the real Lean server on the file given: after 30 s, how
-- far its semantic token marks reach, and how many requests it made.
vim.o.swapfile = false
local reqs, refresh = 0, 0
vim.filetype.add({ extension = { lean = 'lean' } })
vim.cmd('edit ' .. arg_file)
vim.bo.filetype = 'lean'
local id = vim.lsp.start({ name = 'lean', cmd = { 'lean', '--server' }, root_dir = vim.fn.getcwd(),
  handlers = {
    ['workspace/semanticTokens/refresh'] = function(...) refresh = refresh + 1; return vim.lsp.handlers['workspace/semanticTokens/refresh'](...) end,
  },
  on_init = function(c)
    local orig = c.request
    c.request = function(self, method, ...) if method:match('semanticTokens') then reqs = reqs + 1 end; return orig(self, method, ...) end
  end })
vim.wait(wait_ms or 30000, function() return false end, 100)
local ns = vim.api.nvim_get_namespaces()
local last, n = -1, 0
for name, nsid in pairs(ns) do
  if name:match('semantic_tokens') then
    for _, m in ipairs(vim.api.nvim_buf_get_extmarks(0, nsid, 0, -1, {})) do n = n + 1; if m[2] > last then last = m[2] end end
  end
end
-- marks are set only for drawn lines; ask the highlighter's own state too
local st = vim.lsp.semantic_tokens.__STHighlighter and vim.lsp.semantic_tokens.__STHighlighter.active[vim.api.nvim_get_current_buf()]
local hl_last, hl_n = -1, 0
if st then for _, cs in pairs(st.client_state) do for _, t in ipairs((cs.current_result or {}).highlights or {}) do hl_n = hl_n + 1; if t.line > hl_last then hl_last = t.line end end end end
io.stdout:write(string.format('nvim: %d token requests, %d refresh requests from Lean; tokens held: %d, the last on line %d of %d (marks drawn: %d, to line %d)\n',
  reqs, refresh, hl_n, hl_last + 1, vim.api.nvim_buf_line_count(0), n, last + 1))
vim.cmd('qa!')
