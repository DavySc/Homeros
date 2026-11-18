-- usage: nvim --clean --headless -l dump_all.lua <catppuccin dir> <transparent 0|1> <names file>
-- prints "name fg bg sp styles" for each group name in the file; a "@a.b.c" capture
-- with no highlight falls back to its parent, as Neovim's highlighter does.
vim.opt.rtp:prepend(arg[1])
vim.o.termguicolors = true
require("catppuccin").setup({ flavour = "mocha", transparent_background = arg[2] == "1",
  integrations = { snacks = { enabled = true }, mini = { enabled = true }, which_key = true, gitsigns = true,
                   render_markdown = true, native_lsp = { enabled = true } } })
vim.cmd.colorscheme("catppuccin")
local function hex(n) return n and string.format("%d,%d,%d", math.floor(n / 65536), math.floor(n / 256) % 256, n % 256) or "-" end
for g in io.lines(arg[3]) do
  local name, h = g, nil
  while true do
    h = vim.api.nvim_get_hl(0, { name = name, link = false })
    if next(h) or not name:find("%.") or name:sub(1, 1) ~= "@" then break end
    name = name:gsub("%.[^.]*$", "")
  end
  local st = {}
  for _, k in ipairs({"bold","italic","underline","undercurl","strikethrough","reverse"}) do if h[k] then st[#st+1] = k end end
  io.write(string.format("%s fg=%s bg=%s sp=%s st=%s via=%s\n", g, hex(h.fg), hex(h.bg), hex(h.sp), table.concat(st, "+"), name))
end
