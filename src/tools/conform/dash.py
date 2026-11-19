#!/usr/bin/env python3
"""Writes dash-expect.inc: Davy's Neovim dashboard as the terminal shows it,
for tests/test_tools_dash.c.

dash.py SNACKS CATPPUCCIN > dash-expect.inc

SNACKS is folke/snacks.nvim at 882c996 and CATPPUCCIN catppuccin/nvim at
05e8787 (their pins in my-nvim's nvim-pack-lock.json).  Neovim 0.12 runs in
a pty with the dashboard set up as my-nvim's lua/config/core/ui.lua does,
read back with pyte (design/env.nix's).  The changes for homeros, all in the
configuration: the preset keys less c (no config directory), a fixed footer,
and in place of splash.lua's cat a section like its M.section(18): the
picture of src/tools/dash.c (read from there, in the Catppuccin Mocha
colours its roles name) when it fits with 6 columns and 18 lines to spare,
then "h o m e r o s" in lavender."""
import fcntl, os, pty, select, struct, sys, termios, time
import pyte
import pyte.screens

# wcwidth gives the private use area -1, where Neovim (and homeros) count
# one cell: the nerd-font icons would vanish from the screen otherwise
_wcwidth = pyte.screens.wcwidth
pyte.screens.wcwidth = lambda ch: 1 if 0xE000 <= ord(ch) <= 0xF8FF or 0xF0000 <= ord(ch) else _wcwidth(ch)

snacks, cat = sys.argv[1:3]
import re
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dash.c')).read()
body = src[src.index('art[ART_ROWS] = {'):]
body = body[:body.index('};')]
art = [bytes(l, 'utf-8').decode('unicode_escape').encode('latin1').decode('utf-8') for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
ART = '{' + ','.join('{%r,%r}' % (art[k], art[k + 1]) for k in range(0, len(art), 2)) + '}'

INIT = r'''
vim.opt.rtp:prepend(%r)
vim.opt.rtp:prepend(%r)
vim.o.number, vim.o.relativenumber = true, true
require("catppuccin").setup({ transparent_background = true })
vim.cmd.colorscheme("catppuccin")
local P = require("catppuccin.palettes").get_palette("mocha")
-- group names ignore case: the title is Name, not T beside t
local roles = { c = "rosewater", p = "overlay1", f = "green", k = "maroon", m = "peach", s = "yellow", t = "overlay0", b = "mauve", Name = "lavender" }
for r, c in pairs(roles) do vim.api.nvim_set_hl(0, "Homeros" .. r, { fg = P[c], bold = true }) end
local art = %s
local function header()
  local win = vim.api.nvim_get_current_win()
  local width, height = vim.api.nvim_win_get_width(win), vim.api.nvim_win_get_height(win)
  local out = {}
  local w = 0
  for _, row in ipairs(art) do w = math.max(w, vim.fn.strchars(row[1])) end
  if width >= w + 6 and height >= #art + 18 then
    for y, row in ipairs(art) do
      local ch = vim.fn.split(row[1], "\\zs")
      local x = 1
      while x <= #ch do
        local r, j = row[2]:sub(x, x), x
        while j <= #ch and row[2]:sub(j, j) == r do j = j + 1 end
        out[#out + 1] = { table.concat(ch, "", x, j - 1), hl = r ~= " " and ("Homeros" .. r) or nil }
        x = j
      end
      out[#out + 1] = { "\n" }
    end
  end
  for c in ("h o m e r o s"):gmatch(".") do out[#out + 1] = { c, hl = c ~= " " and "HomerosName" or nil } end
  return { align = "center", padding = 1, text = out }
end
require("snacks").setup({ dashboard = { enabled = true,
  preset = { keys = {
    { icon = "\239\128\130 ", key = "f", desc = "Find File", action = ":qa" },
    { icon = "\239\133\155 ", key = "n", desc = "New File", action = ":qa" },
    { icon = "\239\128\162 ", key = "g", desc = "Find Text", action = ":qa" },
    { icon = "\239\131\133 ", key = "r", desc = "Recent Files", action = ":qa" },
    { icon = "\239\144\166 ", key = "q", desc = "Quit", action = ":qa" },
  } },
  sections = {
    header, { section = "keys", gap = 1, padding = 1 },
    { align = "center", text = { { "⚡ Homeros loaded in ", hl = "footer" }, { "1.0ms", hl = "special" } } },
  } } })
''' % (snacks, cat, ART)
init = '/tmp/homeros-dash-init.lua'
open(init, 'w').write(INIT)

legend = []

def code(ch):
    if ch.data in (' ', '~', ''):
        return ' '
    k = ('default' if ch.fg == 'default' else ch.fg.lower()) + ('b' if ch.bold else '')
    if k not in legend:
        legend.append(k)
    return chr(ord('a') + legend.index(k))

def shot(cols, rows):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM='xterm-256color', COLORTERM='truecolor')
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        os.execvp('nvim', ['nvim', '-u', init, '-i', 'NONE', '-n'])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    scr = pyte.Screen(cols, rows)
    st = pyte.ByteStream(scr)

    def drain(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    st.feed(os.read(fd, 1 << 16))
                except OSError:
                    return
    drain(2.0)
    out = []
    for y in range(rows - 1):   # not the command line
        line = scr.buffer[y]
        text = ''.join(line[x].data for x in range(cols))
        out.append((text, ''.join(code(line[x]) for x in range(cols) if line[x].data != '')))
    cur = (scr.cursor.y, scr.cursor.x)
    os.write(fd, b'\x1b:qa!\r')
    drain(0.3)
    os.waitpid(pid, 0)
    return out, cur

def c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

cases = [(100, 40), (101, 40), (80, 24), (120, 50), (200, 60), (60, 20), (40, 15), (30, 10),
         (150, 35), (59, 45), (36, 30), (66, 40), (46, 31), (47, 31), (100, 29), (100, 30), (61, 31)]
body = []
for cols, rows in cases:
    out, cur = shot(cols, rows)
    body.append('\t{ %d, %d, %d, %d, {' % (cols, rows, cur[0], cur[1]))
    for text, colours in out:
        body.append('\t\t{ %s, %s },' % (c(text), c(colours)))
    body.append('\t} },')
print('/* Davy\'s Neovim dashboard on the terminal, by src/tools/conform/dash.py:')
print('   per case the size, the cursor, and per row the text and the')
print('   colour of each cell (a letter of legend[], a blank for none) */')
print('static const char *const legend[] = { %s };' % ', '.join(c(k) for k in legend))
print('static const struct { int cols, rows, crow, ccol; struct { const char *text, *colour; } row[60]; } cases[] = {')
print('\n'.join(body))
print('};')
