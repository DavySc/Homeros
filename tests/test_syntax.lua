-- Neovim's side of tests/test_syntax.c: for each case, open the start text,
-- replay the same edits and parses (on_bytes edits the trees, parse(true)
-- parses as the C side's syntax_update did), then write per byte the face
-- of the winning capture, and whether any tree has a syntax error; at each
-- "f" the faces of the edited, not yet parsed trees go to DIR/out.snap.
-- usage: nvim --headless --clean -l test_syntax.lua OUT DIR CASE...
local ours = { c = true, lua = true, markdown = true, markdown_inline = true }

-- grid.h's faces: NONE 0, COMMENT 1, STRING 2, KEYWORD 3, TYPE 4, FUNCTION 5,
-- CONSTANT 6, NUMBER 7, OPERATOR 8, PUNCT 9, VARIABLE 10, for each of
-- Neovim's standard groups (markup, diff and tag have none yet)
local groups = {
  variable = 10, ['variable.builtin'] = 10, ['variable.parameter'] = 10,
  ['variable.parameter.builtin'] = 10, ['variable.member'] = 10, constant = 6,
  ['constant.builtin'] = 6, ['constant.macro'] = 6, module = 10, ['module.builtin'] = 10,
  label = 10, string = 2, ['string.documentation'] = 2, ['string.regexp'] = 2,
  ['string.escape'] = 2, ['string.special'] = 2, ['string.special.symbol'] = 2,
  ['string.special.path'] = 2, ['string.special.url'] = 2, character = 2,
  ['character.special'] = 2, boolean = 6, number = 7, ['number.float'] = 7, type = 4,
  ['type.builtin'] = 4, ['type.definition'] = 4, attribute = 4, ['attribute.builtin'] = 4,
  property = 10, ['function'] = 5, ['function.builtin'] = 5, ['function.call'] = 5,
  ['function.macro'] = 5, ['function.method'] = 5, ['function.method.call'] = 5,
  constructor = 4, operator = 8, keyword = 3, ['keyword.coroutine'] = 3,
  ['keyword.function'] = 3, ['keyword.operator'] = 3, ['keyword.import'] = 3,
  ['keyword.type'] = 3, ['keyword.modifier'] = 3, ['keyword.repeat'] = 3,
  ['keyword.return'] = 3, ['keyword.debug'] = 3, ['keyword.exception'] = 3,
  ['keyword.conditional'] = 3, ['keyword.conditional.ternary'] = 3,
  ['keyword.directive'] = 3, ['keyword.directive.define'] = 3,
  ['punctuation.delimiter'] = 9, ['punctuation.bracket'] = 9, ['punctuation.special'] = 9,
  comment = 1, ['comment.documentation'] = 1, ['comment.error'] = 1,
  ['comment.warning'] = 1, ['comment.todo'] = 1, ['comment.note'] = 1,
}

-- a capture falls back to its parent group, as "@keyword.return.c" does
local function face(cap)
  if cap:sub(1, 1) == '_' then return 0 end
  while cap ~= '' do
    if groups[cap] then return groups[cap] end
    cap = cap:match('^(.*)%.[^.]*$') or ''
  end
  return 0
end

-- the faces of one redraw of the whole buffer from its first line, as
-- highlighter.lua's on_range_impl draws it: per tree in for_each_tree order,
-- one iter_captures from row 0, each capture's get_range clipped to the
-- tree's included ranges; per byte the highest priority wins, the later
-- capture (and the later tree) on a tie.  (Where captures of one priority
-- overlap, tree-sitter's order between them can depend on the row the
-- iterator starts at, so a check against get_captures_at_pos, which starts
-- at the row asked for, would test something the highlighter does not do.)
local function faces(hl, buf, n)
  local f, pr = {}, {}
  for i = 0, n - 1 do f[i], pr[i] = 0, -1 end
  hl.tree:for_each_tree(function(tstree, ltree)
    if not tstree or not ours[ltree:lang()] then return end
    local q = hl:get_query(ltree:lang()):query()
    if not q then return end
    local root = tstree:root()
    local _, _, er, ec = root:range()
    local region = tstree:included_ranges(true)
    for id, node, md in q:iter_captures(root, buf, 0, er, { start_col = 0, end_col = ec }) do
      local fc = face(q.captures[id])
      if fc > 0 then
        local p = tonumber(md.priority or (md[id] and md[id].priority)) or 100
        local r = vim.treesitter.get_range(node, buf, md[id])
        for _, g in ipairs(region) do
          for i = math.max(r[3], g[3]), math.min(r[6], g[6]) - 1 do
            if p >= pr[i] then f[i], pr[i] = fc, p end
          end
        end
      end
    end
  end)
  return f
end

-- byte offset to (row, col) in the buffer
local function rc(buf, pos)
  local row = vim.fn.byte2line(pos + 1) - 1
  if row < 0 then row = vim.api.nvim_buf_line_count(buf) end
  return row, pos - vim.api.nvim_buf_get_offset(buf, row)
end

-- the faces, one char per byte, '\n' kept
local function render(f)
  local o, i = {}, 0
  for _, line in ipairs(vim.api.nvim_buf_get_lines(0, 0, -1, false)) do
    for col = 1, #line do o[#o + 1] = string.char(48 + f[i + col - 1]) end
    o[#o + 1] = '\n'
    i = i + #line + 1
  end
  return table.concat(o)
end

local function size(buf)
  return vim.api.nvim_buf_get_offset(buf, vim.api.nvim_buf_line_count(buf))
end

local out = assert(io.open(arg[1], 'wb'))
local snap = assert(io.open(arg[2] .. '/out.snap', 'wb'))
local errs = assert(io.open(arg[1] .. '.err', 'wb'))
for k = 3, #arg do
  vim.cmd('edit ++ff=unix ++enc=utf-8 ++bad=keep ' .. vim.fn.fnameescape(arg[2] .. '/' .. arg[k]))
  local buf = vim.api.nvim_get_current_buf()
  vim.treesitter.start(buf)
  local hl = vim.treesitter.highlighter.active[buf]
  hl.tree:parse(true)
  for op in io.lines(arg[2] .. '/' .. arg[k] .. '.ops') do
    if op == 'p' then
      hl.tree:parse(true)
    elseif op == 'f' then
      snap:write('#', arg[k]:match('^%d+'), '\n', render(faces(hl, buf, size(buf))))
    else
      local pos, del, hex = op:match('^e (%d+) (%d+) ?(%x*)$')
      pos, del = tonumber(pos), tonumber(del)
      local ins = hex:gsub('..', function(h) return string.char(tonumber(h, 16)) end)
      local sr, sc = rc(buf, pos)
      local er, ec = rc(buf, pos + del)
      vim.api.nvim_buf_set_text(buf, sr, sc, er, ec, vim.split(ins, '\n', { plain = true }))
    end
  end
  hl.tree:parse(true)
  local err = false
  hl.tree:for_each_tree(function(t, lt)
    if ours[lt:lang()] and t:root():has_error() then err = true end
  end)
  errs:write(err and '1' or '0')
  out:write(render(faces(hl, buf, size(buf))))
  vim.cmd('bwipeout!')
end
out:close()
snap:close()
errs:close()

-- Lua patterns: string.find on each "pattern<TAB>subject" line of OUT.pat
local pf = io.open(arg[1] .. '.pat', 'rb')
if pf then
  local r = assert(io.open(arg[1] .. '.patout', 'wb'))
  for l in pf:lines() do
    local p, s = l:match('^(.-)\t(.*)$')
    local ok, res = pcall(string.find, s, p)
    r:write((ok and res) and '1' or '0')
  end
  r:close()
end
