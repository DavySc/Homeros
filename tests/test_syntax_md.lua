-- Neovim's side of tests/test_syntax_md.c: render-markdown 8.14's facts per
-- line of each DIR/<i>.md, from a walk of every node of Neovim's Markdown
-- block tree (not the C side's lookup at the line's first byte); per line
-- "head list ordered task code mark mend task_col info ninfo", the first
-- node met in document order deciding a line, into DIR/<i>.want.
-- usage: nvim --headless --clean -l test_syntax_md.lua DIR N
local dir, n = arg[1], tonumber(arg[2])
for i = 0, n - 1 do
  local src = io.open(dir .. '/' .. i .. '.md', 'rb'):read('a')
  local ls = { [0] = 0 }
  for p in src:gmatch('()\n') do ls[#ls + 1] = p end
  local L = {}
  local function put(row, f)
    if L[row] then return end
    f.head, f.list, f.ordered, f.task, f.code = f.head or 0, f.list or 0, f.ordered or 0, f.task or 0, f.code or 0
    f.mark, f.mend, f.task_col, f.info, f.ninfo = f.mark or 0, f.mend or 0, f.task_col or 0, f.info or 0, f.ninfo or 0
    L[row] = f
  end
  local function cols(node)
    local sr, _, sb, _, _, eb = node:range(true)
    return sr, sb - ls[sr], eb - ls[sr]
  end
  local function walk(node)
    local t = node:type()
    if t:match('^atx_h%d_marker$') then
      local r, a, z = cols(node)
      put(r, { head = tonumber(t:sub(6, 6)), mark = a, mend = z })
    elseif t:match('^list_marker_') then
      local r, a, z = cols(node)
      local lv, p = 0, node:parent()
      while p do
        if p:type() == 'list' then lv = lv + 1 end
        p = p:parent()
      end
      local f = { list = lv, ordered = (t == 'list_marker_dot' or t == 'list_marker_parenthesis') and 1 or 0, mark = a, mend = z }
      local k = node:next_named_sibling()
      if k and k:type():match('^task_list_marker_') then
        f.task = k:type() == 'task_list_marker_checked' and string.byte('x') or string.byte(' ')
        local _, kc = cols(k)
        f.task_col = kc
      else
        local e = ls[r] + z
        local after = src:sub(e + 4, e + 4)
        if src:sub(e + 1, e + 3) == '[-]' and (after == '' or after == ' ' or after == '\n') then
          f.task, f.task_col = string.byte('-'), z
        end
      end
      put(r, f)
    elseif t == 'fenced_code_block' then
      local d, open, last = {}, nil, nil
      for c in node:iter_children() do
        if c:type() == 'fenced_code_block_delimiter' then d[#d + 1] = c end
      end
      local o = d[1]
      local orow = o and select(1, cols(o)) or select(1, node:range())
      local f = { code = o and 1 or 2 }
      if o then
        local _, a, z = cols(o)
        f.mark, f.mend = a, z
      end
      for c in node:iter_children() do
        if c:type() == 'info_string' then
          local r, a, z = cols(c)
          if r == orow then f.info, f.ninfo = a, z - a end
        end
      end
      -- the body's rows, then the close: rows before walking the children,
      -- so a list inside the block cannot come first
      -- the body: rows whose last byte (a blank row's start) lies inside
      -- the block, up to the close
      local _, _, sb, er, _, eb = node:range(true)
      last = orow
      for r = orow + 1, er do
        local e = (ls[r + 1] or #src + 1) - 1
        local p = e > ls[r] and e - 1 or ls[r]
        if r == er and not ls[r + 1] then p = #src > ls[r] and #src - 1 or ls[r] end
        if p >= sb and p < eb then last = r end
      end
      if d[2] then last = select(1, cols(d[2])) end
      local first = L[orow]
      put(orow, f)
      for r = orow + 1, last do
        if d[2] and r == last then
          local _, a, z = cols(d[2])
          put(r, { code = 3, mark = a, mend = z })
        else
          put(r, { code = 2 })
        end
      end
      if first then L[orow] = first end
    end
    for c in node:iter_children() do walk(c) end
  end
  local root = vim.treesitter.get_string_parser(src, 'markdown'):parse()[1]:root()
  walk(root)
  local out = {}
  for r = 0, #ls do
    local f = L[r]
    out[#out + 1] = f and table.concat({ f.head, f.list, f.ordered, f.task, f.code, f.mark, f.mend, f.task_col, f.info, f.ninfo }, ' ') or '0 0 0 0 0 0 0 0 0 0'
  end
  local o = io.open(dir .. '/' .. i .. '.want', 'wb')
  o:write(table.concat(out, '\n'), '\n')
  o:close()
end
