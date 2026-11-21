-- Neovim's side of tests/test_syntax.c: for each case, open the start text,
-- replay the same edits and parses (on_bytes edits the trees, parse(true)
-- parses as the C side's syntax_update did), then write per byte the face
-- of the winning capture, and whether any tree has a syntax error; at each
-- "f" the faces of the edited, not yet parsed trees go to DIR/out.snap;
-- then each line's foldexpr to OUT.fold.
-- usage: nvim --headless --clean -l test_syntax.lua OUT DIR CASE...
local ours = { c = true, lua = true, markdown = true, markdown_inline = true, rust = true, glsl = true, nix = true, cmake = true, systemverilog = true }
-- the grammars and queries Neovim does not bundle (src/syntax/sources.nix)
vim.opt.rtp:prepend('build/syntax/src/nvim')
-- nvim-treesitter's plugin/filetypes.lua, which Davy's Neovim loads
vim.treesitter.language.register('systemverilog', 'verilog')

-- grid.h's faces: FACE_NONE 0, then Neovim's standard captures in order
local groups = {}
for i, g in ipairs({
  'variable', 'variable.builtin', 'variable.parameter', 'variable.parameter.builtin',
  'variable.member', 'constant', 'constant.builtin', 'constant.macro', 'module',
  'module.builtin', 'label', 'string', 'string.documentation', 'string.regexp',
  'string.escape', 'string.special', 'string.special.symbol', 'string.special.path',
  'string.special.url', 'character', 'character.special', 'boolean', 'number',
  'number.float', 'type', 'type.builtin', 'type.definition', 'attribute',
  'attribute.builtin', 'property', 'function', 'function.builtin', 'function.call',
  'function.macro', 'function.method', 'function.method.call', 'constructor',
  'operator', 'keyword', 'keyword.coroutine', 'keyword.function', 'keyword.operator',
  'keyword.import', 'keyword.type', 'keyword.modifier', 'keyword.repeat',
  'keyword.return', 'keyword.debug', 'keyword.exception', 'keyword.conditional',
  'keyword.conditional.ternary', 'keyword.directive', 'keyword.directive.define',
  'punctuation.delimiter', 'punctuation.bracket', 'punctuation.special', 'comment',
  'comment.documentation', 'comment.error', 'comment.warning', 'comment.todo',
  'comment.note', 'markup.strong', 'markup.italic', 'markup.strikethrough',
  'markup.underline', 'markup.heading', 'markup.heading.1', 'markup.heading.2',
  'markup.heading.3', 'markup.heading.4', 'markup.heading.5', 'markup.heading.6',
  'markup.quote', 'markup.math', 'markup.link', 'markup.link.label', 'markup.link.url',
  'markup.raw', 'markup.raw.block', 'markup.list', 'markup.list.checked',
  'markup.list.unchecked', 'diff.plus', 'diff.minus', 'diff.delta', 'tag',
  'tag.builtin', 'tag.attribute', 'tag.delimiter',
}) do groups[g] = i end

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

-- foldexpr per line, ">n" where a fold starts: vim.treesitter.foldexpr(),
-- or for Markdown its ftplugin's MarkdownFold() ("=" the level above)
vim.g.markdown_folding = 1
local function folds(buf)
  local o, prev = {}, 0
  for l = 1, vim.api.nvim_buf_line_count(buf) do
    local v
    if vim.bo[buf].filetype == 'markdown' then
      vim.v.lnum = l
      v = vim.fn.MarkdownFold()
      if v == '=' then v = tostring(prev) end
    else
      v = vim.treesitter.foldexpr(l)
    end
    prev = tonumber((v:gsub('>', '')))
    o[#o + 1] = v
  end
  return table.concat(o, ' ') .. '\n'
end

local out = assert(io.open(arg[1], 'wb'))
local fo = assert(io.open(arg[1] .. '.fold', 'wb'))
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
  fo:write(folds(buf))
  vim.cmd('bwipeout!')
end
out:close()
fo:close()
snap:close()
errs:close()

-- Lua patterns: string.find, then string.gsub, on each
-- "replacement<TAB>pattern<TAB>subject" line of OUT.pat
local pf = io.open(arg[1] .. '.pat', 'rb')
if pf then
  local r = assert(io.open(arg[1] .. '.patout', 'wb'))
  for l in pf:lines() do
    local rep, p, s = l:match('^(.-)\t(.-)\t(.*)$')
    local ok, res = pcall(string.find, s, p)
    local gok, g = pcall(string.gsub, s, p, rep)
    r:write((ok and res) and '1' or '0', gok and g:gsub('.', function(c) return ('%02x'):format(c:byte()) end) or '!', '\n')
  end
  r:close()
end
