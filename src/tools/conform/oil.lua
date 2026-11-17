-- Oracle for src/tools/oil.c: oil.nvim lists, renders and turns edited
-- lines into actions.  nvim --clean --headless -l oil.lua OIL MINI SEED N [slash]
-- (slash: every link target ends in "/"; without it older seeds replay)
-- Each case: a random tree under /tmp/oc, two oil buffers (the root and a
-- subdirectory), random edits; prints per case
--   C seed / R buf line (rendered) / E buf line (edited) /
--   A type etype src dst / X buf line message; when there are actions, oil
--   runs them: D ok|err / T path type [-> link | contents] (the tree after) /
--   B buf dir / S buf line (each buffer's directory, as moves renamed it, rendered again); then "." at the end.
vim.opt.rtp:prepend(arg[1])
vim.opt.rtp:prepend(arg[2])
require("mini.icons").setup()
require("oil").setup({ cleanup_delay_ms = false, columns = { "icon" }, view_options = { show_hidden = true }, skip_confirm_for_simple_edits = false })
local cache = require("oil.cache")
local parser = require("oil.mutator.parser")
local mutator = require("oil.mutator")
local view = require("oil.view")
local uv = vim.uv
-- the tree under p, sorted: "T path type [-> link | contents]"
local function tree(p)
  local names = {}
  for nm in vim.fs.dir(p) do names[#names + 1] = nm end
  table.sort(names)
  for _, nm in ipairs(names) do
    local q = p .. "/" .. nm
    local st = uv.fs_lstat(q)
    if st.type == "link" then io.write("T ", q, " link -> ", uv.fs_readlink(q), "\n")
    elseif st.type == "directory" then io.write("T ", q, " directory\n"); tree(q)
    else io.write("T ", q, " file ", (table.concat(vim.fn.readfile(q), "|")), "\n") end
  end
end
local seed, n = tonumber(arg[3]), tonumber(arg[4])
local tail = arg[5] == "slash" and "/" or ""
local NAMES = { "a.c", "b.lua", "README.md", "Makefile", "file9.txt", "file10.txt", "x", "sub", ".hidden",
  "deep", "z.tar.gz", "Ä.md", "notes.org", "img.png", "a b", "UPPER", "007", "v2.0.1" }
local function rnd(t) return t[math.random(#t)] end
local out = io.stdout
local function wait_render(buf)
  vim.wait(3000, function()
    return vim.api.nvim_buf_is_loaded(buf) and not vim.b[buf].oil_rendering and vim.api.nvim_buf_get_lines(buf, 0, 1, false)[1] ~= ""
  end, 5)
end
for c = 1, n do
  math.randomseed(seed * 1000 + c)
  vim.fn.system({ "rm", "-rf", "/tmp/oc" })
  vim.fn.mkdir("/tmp/oc/sub", "p")
  for _ = 1, math.random(0, 10) do
    local nm = rnd(NAMES)
    local p = (math.random() < 0.3 and "/tmp/oc/sub/" or "/tmp/oc/") .. nm
    if not vim.uv.fs_lstat(p) then
      if math.random() < 0.25 then vim.fn.mkdir(p, "p")
      elseif math.random() < 0.3 then vim.uv.fs_symlink(rnd(NAMES) .. tail, p)
      else vim.fn.writefile({ nm }, p) end
    end
  end
  cache.clear_everything()
  vim.cmd("silent! %bwipeout!")
  out:write("C ", seed * 1000 + c, "\n")
  local bufs = {}
  for b, dir in ipairs({ "/tmp/oc/", "/tmp/oc/sub/" }) do
    vim.cmd("edit oil://" .. dir)
    local buf = vim.api.nvim_get_current_buf()
    wait_render(buf)
    bufs[b] = buf
    for _, l in ipairs(vim.api.nvim_buf_get_lines(buf, 0, -1, false)) do out:write("R ", b, " ", l, "\n") end
  end
  -- the cache, by id: K id dir type linkdir name [-> link]
  for id = 1, 1000 do
    local e = cache.get_entry_by_id(id)
    if not e then break end
    local meta = e[4] or {}
    local ld = meta.link_stat and meta.link_stat.type == "directory" and 1 or 0
    out:write(("K %d %s %s %d %s%s\n"):format(id, cache.get_parent_url(id):gsub("^oil://", ""), e[3], ld, e[2], meta.link and (" -> " .. meta.link) or ""))
  end
  -- random edits on the lines
  local lines = { vim.api.nvim_buf_get_lines(bufs[1], 0, -1, false), vim.api.nvim_buf_get_lines(bufs[2], 0, -1, false) }
  for _ = 1, math.random(1, 4) do
    local b = math.random(2)
    local L = lines[b]
    local k = math.random(10)
    local i = math.random(math.max(#L, 1))
    if k == 1 and #L > 0 then table.remove(L, i)                                       -- delete
    elseif k == 2 and L[i] then L[i] = L[i]:gsub("[^ ]+$", math.random() < 0.1 and "/" or rnd(NAMES))                -- rename
    elseif k == 3 and L[i] then L[i] = L[i] .. "/"                                     -- to a directory
    elseif k == 4 then table.insert(L, i, rnd({ "new.txt", "nd/", "a/b/c.txt", "x.{c,h}", "ln -> a.c", rnd(NAMES), "/abs", "  ", "a/b/", "p{1,2}{3,4}", "q//r", "{}{s,t}", "u.{v,}", "sub//" }))
    elseif k == 5 and L[i] then table.insert(lines[3 - b], math.random(#lines[3 - b] + 1), L[i])  -- copy to the other
    elseif k == 6 and L[i] then table.insert(lines[3 - b], math.random(#lines[3 - b] + 1), table.remove(L, i))  -- move
    elseif k == 7 and L[i] then table.insert(L, i, L[i])                               -- duplicate
    elseif k == 9 and #L > 2 then                                                       -- swap two names: a move cycle
      local j = math.random(2, #L)
      local a, b = L[i]:match("[^ ]+$"), L[j]:match("[^ ]+$")
      if a and b then L[i], L[j] = L[i]:gsub("[^ ]+$", (b:gsub("%%", "%%%%"))), L[j]:gsub("[^ ]+$", (a:gsub("%%", "%%%%"))) end
    elseif k == 10 then                                                                 -- the subdirectory and all in it
      for j = #lines[1], 1, -1 do if lines[1][j]:match(" sub/$") then table.remove(lines[1], j) end end
      lines[2] = { lines[2][1] }
    elseif k == 8 then table.insert(L, i, rnd({ "/12x y", "/999 x  y", "/001 ", "/001 x", "/0001 x  " .. rnd(NAMES) }))
    end
  end
  local all, errors = {}, {}
  for b = 1, 2 do
    vim.bo[bufs[b]].modifiable = true
    vim.api.nvim_buf_set_lines(bufs[b], 0, -1, true, lines[b])
    for _, l in ipairs(lines[b]) do out:write("E ", b, " ", l, "\n") end
    local diffs, errs = parser.parse(bufs[b])
    all[bufs[b]] = diffs
    for _, e in ipairs(errs) do out:write("X ", b, " ", e.lnum, " ", e.message, "\n") end
    errors[#errors + 1] = #errs
  end
  if errors[1] + errors[2] == 0 then
    -- oil walks buffers, entry ids and a trie's children with pairs, so
    -- their order is LuaJIT's hash order (its string hash is seeded at
    -- random); walk keys in ascending order instead, which is what oil.c
    -- does, so the order of actions can be compared
    local raw = pairs
    pairs = function(t)
      local keys, kind = {}, nil
      for k in raw(t) do
        kind = kind or type(k)
        if type(k) ~= kind or (kind ~= "number" and kind ~= "string") then return raw(t) end
        keys[#keys + 1] = k
      end
      table.sort(keys)
      local i = 0
      return function() i = i + 1; return keys[i], keys[i] and t[keys[i]] end, t, nil
    end
    local ok, acts = pcall(mutator.create_actions_from_diffs, all)
    pairs = raw
    if not ok then out:write("X 0 -1 ", (tostring(acts):gsub("^.-:%d+: ", "")), "\n")
    else
      for _, a in ipairs(acts) do
        local src = (a.url or a.src_url):gsub("^oil://", "")
        local dst = (a.dest_url or "-"):gsub("^oil://", "")
        out:write("A ", a.type, " ", a.entry_type, " ", src, " ", dst, "\n")
      end
      if #acts > 0 then
        local done, err = false, nil
        -- util.update_moved_buffers matches a moved directory as a bare
        -- prefix, so moving "sub" renames the buffer of "sub__oil_tmp_1/"
        -- too (every directory cycle does it): oil.c takes whole names;
        -- here vim.startswith does too, while oil runs the actions
        local sw = vim.startswith
        vim.startswith = function(str, pre)
          return sw(str, pre) and (#str == #pre or pre:sub(-1) == "/" or str:sub(#pre + 1, #pre + 1) == "/")
        end
        mutator.process_actions(acts, function(e) done, err = true, e end)
        vim.wait(5000, function() return done end, 5)
        vim.startswith = sw
        out:write("D ", err or "ok", "\n")
        tree("/tmp/oc")
        for b = 1, 2 do
          local fin = false
          if vim.api.nvim_buf_is_valid(bufs[b]) then
            out:write("B ", b, " ", vim.api.nvim_buf_get_name(bufs[b]):gsub("^oil://", ""), "\n")
            view.render_buffer_async(bufs[b], { refetch = true }, function() fin = true end)
            vim.wait(3000, function() return fin end, 5)
            for _, l in ipairs(vim.api.nvim_buf_get_lines(bufs[b], 0, -1, false)) do
              -- an error is drawn centred in the window: only its text
              if l ~= "" then out:write("S ", b, " ", (l:gsub("^%s+Error:", "Error:")), "\n") end
            end
          end
        end
      end
    end
  end
  for _, b in ipairs(bufs) do vim.bo[b].modified = false end
end
out:write(".\n")
