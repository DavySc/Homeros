-- Oracle for src/tools/match.c: snacks.picker's own matcher scores the
-- cases.  nvim --clean --headless -l match.lua SNACKS_DIR IN OUT
-- IN: one JSON case per line {p = pattern, i = {{t = text, f = file|nil}...}}
-- OUT: per case "F line col" then one score per item.
local snacks, input, output = arg[1], arg[2], arg[3]
vim.opt.rtp:prepend(snacks)
require("snacks").setup({})
-- the picker's matcher options (config/defaults.lua: matcher)
local opts = { fuzzy = true, smartcase = true, ignorecase = true, filename_bonus = true, file_pos = true }
local out = assert(io.open(output, "w"))
for line in io.lines(input) do
  local c = vim.json.decode(line)
  local m = require("snacks.picker.core.matcher").new(opts)
  m:init(c.p)
  out:write(("F %d %d\n"):format(m.file and m.file.pos[1] or 0, m.file and m.file.pos[2] or 0))
  for i, it in ipairs(c.i) do
    local item = { text = it.t, file = it.f ~= vim.NIL and it.f or nil, idx = i, score = 0 }
    out:write(m:match(item), "\n")
  end
end
out:close()
