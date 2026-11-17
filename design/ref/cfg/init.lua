vim.api.nvim_create_autocmd("BufEnter", { pattern = "*.fake", callback = function(a)
  vim.lsp.start({ name = "fake", cmd = { "python3", "/opt/ne/ref/fakelsp.py" }, root_dir = "/opt/ne/ref/w" })
end })
