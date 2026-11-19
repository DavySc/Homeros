-- tests/test_keys_regex.c's cases in Neovim: nvim --headless --clean -l
-- src/keys/regex_oracle.lua CASES OUT ENGINE SKIP.  A case is three lines: 'ignorecase'
-- (0 or 1), the pattern, the text; OUT gets substitute(text, pattern,
-- '[&]', 'g') for each after the first SKIP, a line at a time so a caller
-- that kills a loop knows where it was, or "E" when the pattern is an error, "T" when it
-- takes too much memory (E363), with 'regexpengine' ENGINE.  (match() and
-- vim.regex match from a start column on the rest of the line only, so ^
-- matches there; substitute() keeps the whole line, as :s and / do.)
local out = io.open(_G.arg[2], 'a')
out:setvbuf('line')
local f = io.lines(_G.arg[1])
vim.o.regexpengine = tonumber(_G.arg[3])
for _ = 1, 3 * tonumber(_G.arg[4]) do f() end
while true do
  local ic = f()
  if not ic then break end
  local pat, text = f(), f()
  vim.o.ignorecase = ic == '1'
  local ok, r = pcall(vim.fn.substitute, text, pat, '[&]', 'g')
  out:write(ok and r or r:find('E363') and 'T' or 'E', '\n')
end
out:close()
vim.cmd('qa!')
