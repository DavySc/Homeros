-- nvim --clean --headless -l hl.lua <catppuccin dir> <transparent 0|1>
vim.opt.rtp:prepend(arg[1])
vim.o.termguicolors = true
require("catppuccin").setup({ flavour = "mocha", transparent_background = arg[2] == "1",
  integrations = { snacks = { enabled = true }, mini = { enabled = true }, which_key = true, gitsigns = true, native_lsp = { enabled = true } } })
vim.cmd.colorscheme("catppuccin")
local function hex(n) return n and string.format("#%06x", n) or "-" end
for _, g in ipairs({"NonText","SpecialKey","EndOfBuffer","Whitespace","PmenuSbar","PmenuThumb","PmenuSel","Pmenu","PmenuKind","PmenuExtra","PmenuMatch","PmenuMatchSel"}) do
  local raw = vim.api.nvim_get_hl(0, { name = g })
  local h = vim.api.nvim_get_hl(0, { name = g, link = false })
  local st = {}
  for _, k in ipairs({"bold","italic","underline","undercurl","reverse","blend","nocombine"}) do if h[k] then st[#st+1] = k end end
  io.write(string.format("%-14s fg=%s bg=%s sp=%s %s%s\n", g, hex(h.fg), hex(h.bg), hex(h.sp), table.concat(st, ","), raw.link and (" link=" .. raw.link) or ""))
end
