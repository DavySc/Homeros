-- Neovim's side of tests/test_json.c: per byte of each DIR/<i>.json, the
-- winning capture of nvim-treesitter's JSON highlights on tree-sitter-json,
-- as Neovim's highlighter picks it (the highest priority, the later
-- capture on a tie), in test_json.c's letters, into DIR/<i>.want; "!"
-- first when the tree has a syntax error.
-- usage: nvim --headless --clean -l test_json.lua PARSER QUERY DIR N
local so, qf, dir, n = arg[1], arg[2], arg[3], tonumber(arg[4])
vim.treesitter.language.add('json', { path = so })
local q = vim.treesitter.query.parse('json', io.open(qf):read('a'))
-- captures without a colour (conceal, spell) are left out, as there
local code = {
  boolean = 'B', ['constant.builtin'] = 'L', number = 'N', property = 'P', string = 'S',
  ['string.escape'] = 'E', comment = 'C', ['punctuation.delimiter'] = 'D', ['punctuation.bracket'] = 'R',
}
for i = 0, n - 1 do
  local path = dir .. '/' .. i .. '.json'
  local src = io.open(path, 'rb'):read('a')
  local root = vim.treesitter.get_string_parser(src, 'json'):parse()[1]:root()
  local f, pr = {}, {}
  for j = 1, #src do f[j], pr[j] = '.', -1 end
  for id, node, md in q:iter_captures(root, src) do
    local c = code[q.captures[id]]
    if c then
      local p = tonumber(md.priority or (md[id] and md[id].priority)) or 100
      local _, _, a, _, _, z = node:range(true)
      for j = a + 1, z do
        if p >= pr[j] then f[j], pr[j] = c, p end
      end
    end
  end
  local o = io.open(dir .. '/' .. i .. '.want', 'wb')
  o:write((root:has_error() and '!' or '') .. table.concat(f))
  o:close()
end
