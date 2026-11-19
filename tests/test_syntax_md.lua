-- Neovim's side of tests/test_syntax_md.c: render-markdown 8.14's facts per
-- line of each DIR/<i>.md, from a walk of every node of Neovim's Markdown
-- block tree (not the C side's lookup at the line's first byte): per row the
-- first heading marker, the first list marker and the fenced code block met
-- in document order; per line "head list ordered task code mark mend item
-- iend index task_col info ninfo lang nlang" into DIR/<i>.want.
-- usage: nvim --headless --clean -l test_syntax_md.lua DIR N
local dir, n = arg[1], tonumber(arg[2])
for i = 0, n - 1 do
  local src = io.open(dir .. '/' .. i .. '.md', 'rb'):read('a')
  local ls = { [0] = 0 }
  for p in src:gmatch('()\n') do ls[#ls + 1] = p end
  local H, Li, C, dash = {}, {}, {}, {}
  local function cols(node)
    local sr, _, sb, _, _, eb = node:range(true)
    return sr, sb - ls[sr], eb - ls[sr]
  end
  local function walk(node)
    local t = node:type()
    if t:match('^atx_h%d_marker$') then
      local r, a, z = cols(node)
      H[r] = H[r] or { head = tonumber(t:sub(6, 6)), mark = a, mend = z }
    elseif t:match('^list_marker_') then
      local r, a, z = cols(node)
      local lv, p = 0, node:parent()
      while p do
        if p:type() == 'list' then lv = lv + 1 end
        p = p:parent()
      end
      local f = { list = lv, ordered = 0, item = a, iend = z, index = 0, task = 0, task_col = 0 }
      if t == 'list_marker_dot' or t == 'list_marker_parenthesis' then
        f.ordered, p = 1, node:parent()
        while p and p:type() == 'list_item' do f.index, p = f.index + 1, p:prev_sibling() end
      end
      local k = node:next_named_sibling()
      if k and k:type():match('^task_list_marker_') then
        f.task = k:type() == 'task_list_marker_checked' and string.byte('x') or string.byte(' ')
        f.task_col = select(2, cols(k))
      elseif dash[r .. ':' .. z] then
        f.task, f.task_col = string.byte('-'), z
      end
      Li[r] = Li[r] or f
    elseif t == 'fenced_code_block' then
      local sr, sc, sb, er, _, eb = node:range(true)
      -- render-markdown skips a block whose Node:height() (end_row -
      -- start_row + 1) is 2 or less
      if er - sr > 1 then
        local d = {}
        for c in node:iter_children() do
          if c:type() == 'fenced_code_block_delimiter' then d[#d + 1] = c end
        end
        local orow = d[1] and cols(d[1]) or sr
        local f = { code = d[1] and 1 or 2, mark = sc, mend = 0, info = 0, ninfo = 0, lang = 0, nlang = 0 }
        if d[1] then f.mark, f.mend = select(2, cols(d[1])) end
        for c in node:iter_children() do
          if c:type() == 'info_string' and cols(c) == orow then
            local _, a, z = cols(c)
            f.info, f.ninfo = a, z - a
            local g = c:named_child(0)
            if g and g:type() == 'language' then
              local _, ga, gz = cols(g)
              f.lang, f.nlang = ga, gz - ga
            end
          end
        end
        C[orow] = C[orow] or f
        -- code.lua's background(start_row + 1, end_row - 2), and the close
        if d[2] then
          local r, a, z = cols(d[2])
          C[r] = C[r] or { code = 3, mark = a, mend = z }
        end
        for r = sr + 1, er - 2 do C[r] = C[r] or { code = 2, mark = sc, mend = 0 } end
      end
    end
    for c in node:iter_children() do walk(c) end
  end
  local lt = vim.treesitter.get_string_parser(src, 'markdown')
  local root = lt:parse(true)[1]:root()
  -- shortcut.lua's custom checkbox: a shortcut_link "[-]" with ' ' after
  -- it (the inline trees), keyed by its row and start column
  local function links(node)
    if node:type() == 'shortcut_link' then
      local r, c, er, ec = node:range()
      local line = src:sub(ls[r] + 1, (ls[r + 1] or #src + 1) - 1)
      if er == r and line:sub(c + 1, ec) == '[-]' and line:sub(ec + 1, ec + 1) == ' ' then dash[r .. ':' .. c] = true end
    end
    for c in node:iter_children() do links(c) end
  end
  for _, t in pairs(lt:children().markdown_inline and lt:children().markdown_inline:trees() or {}) do links(t:root()) end
  walk(root)
  local out = {}
  for r = 0, #ls do
    local h, l, c = H[r] or {}, Li[r] or {}, C[r] or {}
    local m = c.code and c or h
    out[#out + 1] = table.concat({ h.head or 0, l.list or 0, l.ordered or 0, l.task or 0, c.code or 0,
      m.mark or 0, m.mend or 0, l.item or 0, l.iend or 0, l.index or 0, l.task_col or 0,
      c.info or 0, c.ninfo or 0, c.lang or 0, c.nlang or 0 }, ' ')
  end
  local o = io.open(dir .. '/' .. i .. '.want', 'wb')
  o:write(table.concat(out, '\n'), '\n')
  o:close()
end
