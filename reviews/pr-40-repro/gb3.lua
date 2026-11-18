-- every sequence of up to 4 tokens: cluster starts by Neovim's utfc_ptr2len
local ffi = require('ffi')
ffi.cdef[[int utfc_ptr2len(const char *p);]]
local T = { "\r", "\n", "a", "\192\141", "\192\138", "\224\128\141", "\224\128\138", "\240\128\128\138", "\240\128\128\141",
  "\204\129", "\226\128\141", "\224\164\149", "\224\165\141", "\217\132", "\216\167", "\240\159\145\168",
  "\224\164\131", "\216\128", "\194\133", "\128", "\234\176\128", "\225\132\128" }
local f = io.open(arg[1], 'w')
local function emit(s)
  local p, i, t = ffi.cast('const char *', s), 0, {}
  while i < #s do t[#t + 1] = i; i = i + ffi.C.utfc_ptr2len(p + i) end
  f:write((s:gsub('.', function(c) return string.format('%02x', c:byte()) end)), ' ', table.concat(t, ' '), '\n')
end
local function rec(s, d)
  if #s > 0 then emit(s) end
  if d == 0 then return end
  for _, t in ipairs(T) do rec(s .. t, d - 1) end
end
rec('', 4)
f:close()
