-- Neovim for tests/test_core_markdown.py: Davy's render-markdown look
-- (render-markdown.nvim 8.14 with catppuccin mocha, opaque background as Davy chose,
-- its render_markdown integration), cut to what the core draws: ATX headings,
-- bullets, checkboxes and fenced code, with no sign column and conceallevel
-- 0; 'cursorline', as the core draws it (Davy's pick of the UI proposals);
-- the rest of FEATURES.md's settings as
-- test_core_screens.py.  RM_RTP: the two plugins' directories, ':'-separated.
for d in vim.gsplit(os.getenv('RM_RTP'), ':') do vim.opt.rtp:prepend(d) end
vim.g.loaded_matchparen = 1
vim.g.markdown_recommended_style = 0   -- the ftplugin's tabstop=4
vim.o.termguicolors = true
vim.cmd('set noswapfile nomore shortmess+=I expandtab shiftwidth=4 softtabstop=4 autoindent smarttab nostartofline number relativenumber cursorline scrolloff=6 splitright splitbelow nowrap')
require('catppuccin').setup({ flavour = 'mocha', transparent_background = false, integrations = { render_markdown = true } })
vim.cmd.colorscheme('catppuccin')
local off = { enabled = false }
require('render-markdown').setup({
  heading = { sign = false, setext = false },
  quote = off, pipe_table = off, link = off, dash = off,
  sign = off, inline_highlight = off, html = off, latex = off, indent = off, document = off,
  paragraph = off, yaml = off,
  win_options = { conceallevel = { rendered = 0 } },
})
