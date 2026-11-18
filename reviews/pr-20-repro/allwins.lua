local r = {}
for _, w in ipairs(vim.api.nvim_tabpage_list_wins(0)) do
  local v = vim.api.nvim_win_call(w, function() return vim.fn.winsaveview() end)
  r[#r + 1] = string.format('win h%d w%d buf%d line %d col %d top %d skip %d cur=%s', vim.api.nvim_win_get_height(w), vim.api.nvim_win_get_width(w), vim.api.nvim_win_get_buf(w), v.lnum, v.col, v.topline, v.skipcol, tostring(w == vim.api.nvim_get_current_win()))
end
return r
