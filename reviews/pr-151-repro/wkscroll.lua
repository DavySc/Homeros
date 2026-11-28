-- which-key.nvim v3.17.0 win.lua M:scroll(), its body run on a float of
-- `height` rows over a buffer of `lines` lines (as the panel: scrolloff 0)
local function run(height, lines, keys)
  local buf = vim.api.nvim_create_buf(false, true)
  local t = {}
  for i = 1, lines do t[i] = tostring(i) end
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, t)
  local win = vim.api.nvim_open_win(buf, false, { relative = "editor", row = 1, col = 0, width = 20, height = height, style = "minimal" })
  vim.wo[win].scrolloff = 0
  local out = {}
  for up in keys:gmatch(".") do
    up = up == "u"
    local h = vim.api.nvim_win_get_height(win)
    local delta = math.ceil((up and -1 or 1) * h / 2)
    local view = vim.api.nvim_win_call(win, vim.fn.winsaveview)
    local top = view.topline
    top = top + delta
    top = math.max(top, 1)
    top = math.min(top, vim.api.nvim_buf_line_count(buf) - h + 1)
    vim.api.nvim_win_call(win, function() vim.fn.winrestview({ topline = top, lnum = top }) end)
    out[#out + 1] = vim.api.nvim_win_call(win, vim.fn.winsaveview).topline - 1
  end
  print(("height %d, %d lines, keys %s: first line shown (from 0) %s"):format(height, lines, keys, table.concat(out, " ")))
  vim.api.nvim_win_close(win, true)
end
vim.o.lines = 40
run(5, 14, "du")
run(5, 14, "dddu")
run(4, 7, "dduu")
run(7, 14, "ddu")
