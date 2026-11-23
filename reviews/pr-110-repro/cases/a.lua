local function f()
  if a then
    b()
  else
    c()
  end
  return {
    1,
  }
end
vim.cmd([[
  augroup x
  augroup END
]])
