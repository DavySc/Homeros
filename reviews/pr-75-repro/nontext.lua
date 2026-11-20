-- nvim --clean --headless -l nontext.lua : which group draws each not-text cell.
-- Finds the cell by its character in a screen row, so no geometry is assumed.
vim.o.termguicolors = true; vim.o.swapfile = false
vim.cmd("hi NonText guifg=#111111 | hi SpecialKey guifg=#222222 | hi EndOfBuffer guifg=#333333 | hi Whitespace guifg=#444444")
local name = { [0x111111] = "NonText", [0x222222] = "SpecialKey", [0x333333] = "EndOfBuffer", [0x444444] = "Whitespace" }
local W, H = vim.o.columns, vim.api.nvim_win_get_height(0)
local function find(what, ch, r0, r1)   -- first cell showing ch in rows r0..r1
  vim.cmd("redraw!")
  for r = r0, r1 do for c = 0, W - 1 do
    local x = vim.api.nvim__inspect_cell(1, r, c)
    if x[1] == ch then
      io.write(string.format("%-36s row %2d col %2d %-5s %s\n", what, r, c, vim.inspect(ch), name[(x[2] or {}).foreground] or "other")); return
    end
  end end
  io.write(string.format("%-36s %s not on screen\n", what, vim.inspect(ch)))
end
local function lines(l) vim.api.nvim_buf_set_lines(0, 0, -1, false, l); vim.cmd("normal! gg0") end
io.write("columns " .. W .. " window height " .. H .. "\n")
-- the first frame inspected after the :hi commands shows another colour (#b3f6c0, none of the four) for the ^A cell; the same line drawn again shows SpecialKey. Cause not pursued.
lines({ "a\1b" }); find("^A control char (first look)", "^", 0, 0); lines({ "q\1b" }); find("^A control char", "^", 0, 0)
lines({ "a\x80b" }); find("<80> invalid byte", "<", 0, 0)
lines({ "a\u{9f}b" }); find("<9f> C1", "<", 0, 0)
vim.cmd("set wrap"); lines({ string.rep("x", W - 1) .. "漢字" }); find("'>' wide char does not fit at eol", ">", 0, 0)
vim.cmd("set nowrap"); lines({ "漢字xxxx" }); vim.cmd("normal! zl"); find("'<' wide char cut by leftcol", "<", 0, 0)
vim.cmd("set wrap display=lastline"); lines({ "one", string.rep("y", W * (H + 3)) }); find("'@@@' display=lastline", "@", 0, H - 1)
vim.cmd("set display="); lines({ "one", string.rep("y", W * (H + 3)) }); find("'@' display= (line does not fit)", "@", 0, H - 1)
lines({ "one" }); find("'~' past the end", "~", 0, H - 1)
vim.cmd("set showbreak=+ wrap"); lines({ string.rep("z", W + 10) }); find("showbreak", "+", 0, 2)
vim.cmd("set showbreak= nowrap list listchars=tab:>-,trail:.,eol:$,nbsp:_,extends:#,precedes:!")
lines({ "\ta\u{a0}b " }); find("listchars tab", ">", 0, 0); find("listchars nbsp", "_", 0, 0); find("listchars trail", ".", 0, 0); find("listchars eol", "$", 0, 0)
lines({ string.rep("w", W + 30) }); find("listchars extends", "#", 0, 0); vim.cmd("normal! 20zl"); find("listchars precedes", "!", 0, 0)
vim.cmd("set nolist"); lines({ "x" }); vim.cmd("vsplit | wincmd l | diffthis | wincmd h"); lines({ "x", "p", "q" }); vim.cmd("diffthis"); find("diff filler '-'", "-", 0, H - 1)
