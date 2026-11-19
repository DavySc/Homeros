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
  local H, Li, C = {}, {}, {}
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
      else
        local e = ls[r] + z
        local after = src:sub(e + 4, e + 4)
        if src:sub(e + 1, e + 3) == '[-]' and (after == '' or after == ' ' or after == '\n') then
          f.task, f.task_col = string.byte('-'), z
        end
      end
      Li[r] = Li[r] or f
    elseif t == 'fenced_code_block' then
      local sr, sc, sb, er, _, eb = node:range(true)
      -- render-markdown skips a block of 2 rows or fewer
      if er - sr > 2 then
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
        -- the body: rows whose last byte (a blank row's start) lies inside
        -- the block, up to the close
        local last = orow
        for r = orow + 1, er do
          local e = (ls[r + 1] or #src + 1) - 1
          local p = e > ls[r] and e - 1 or ls[r]
          if r == er and not ls[r + 1] then p = #src > ls[r] and #src - 1 or ls[r] end
          if p >= sb and p < eb then last = r end
        end
        if d[2] then last = cols(d[2]) end
        for r = orow + 1, last do
          if d[2] and r == last then
            local _, a, z = cols(d[2])
            C[r] = C[r] or { code = 3, mark = a, mend = z }
          else
            C[r] = C[r] or { code = 2, mark = sc, mend = 0 }
          end
        end
      end
    end
    for c in node:iter_children() do walk(c) end
  end
  local root = vim.treesitter.get_string_parser(src, 'markdown'):parse()[1]:root()
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
