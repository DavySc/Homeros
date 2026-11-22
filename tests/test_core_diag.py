#!/usr/bin/env python3
"""Records diagnostics as Neovim 0.12 draws them with its defaults (signs,
underline, 'signcolumn' auto) and virtual_text = {current_line = true}
(Davy's pick), for test_core_diag.c.
usage: test_core_diag.py N [SEED] --record FILE
       test_core_diag.py --serve FILE   (the fake language server)
Each case: a random text, random diagnostics (as lsp.h keeps them: sorted
by start, no two with one start, an empty one a point), a window of random
size with or without wrap, the cursor and 'leftcol' at random.  Records
each window row's characters (screenstring()) and, per cell, which
diagnostic highlight it has (screenattr() against a cell highlighted with each
group alone, each group given its own colour): u1..u4
DiagnosticUnderline*, v1..v4 DiagnosticVirtualText*, s1..s4
DiagnosticSign*, '.' none; u3/u2 where underlines of those colours start
at one place (below).  As the fake server it answers initialize and
publishes case k's diagnostics when k.diag is opened.
Underlines that start at one place: Neovim draws them in its marktree's
order, which follows the B-tree's history (measured: [0,2) info and [0,1)
warn on one line drawn info in one buffer, warn in another), so the cells
they share accept any of their colours; the core draws the first.
Known difference (measured: seed 2, 500 cases, 1 differs): a Tab after a
double-width character that wrapped with a '>' in a row that skipcol
hides (not diagnostics: the core's wrap).  Neovim measures the Tab as if
the '>' were not there, one cell wider than when the row shows.  Reduced:
seed 2 case 299.
Known differences the generator does not make (review of PR 120, repro in
the shared reviews/pr-120-repro/, measured once each):
- Insert mode: the message follows the cursor; Neovim leaves it on the line
  of the last Normal-mode CursorMoved (current_line hooks CursorMoved only).
- Sign and message take the last diagnostic by position; Neovim takes the
  last one the server sent (lsp.h keeps them sorted by start, so the order
  is lost there: package 13's and thread 0's to change).
- One line's underlines: the core keeps the 64 that start last (a fixed
  array: no allocation while drawing), Neovim draws them all (100
  one-character diagnostics: 100 cells underlined, the core 64).
- A window on a buffer that is not the current one: Neovim keeps the
  message where that buffer's last CursorMoved put it, the core puts it on
  that window's cursor line."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile
from test_core_screens import SETUP

LUA = r'''
local c = vim.json.decode(io.open(IN):read('*a'))
-- the core draws no MatchParen yet, which would hide an underline
vim.cmd('NoMatchParen')
vim.o.lines = c.rows
vim.o.columns = c.cols
vim.o.wrap = c.wrap
for s = 1, 4 do
  local n = ({'Error', 'Warn', 'Info', 'Hint'})[s]
  vim.api.nvim_set_hl(0, 'DiagnosticUnderline' .. n, {undercurl = true, sp = s})
  vim.api.nvim_set_hl(0, 'DiagnosticVirtualText' .. n, {fg = 16 + s})
  vim.api.nvim_set_hl(0, 'DiagnosticSign' .. n, {fg = 32 + s})
end
vim.diagnostic.config({virtual_text = {current_line = true}})
local ns = vim.api.nvim_create_namespace('t')
vim.diagnostic.set(ns, 0, c.diags)
-- a redraw sizes the sign column, which the view then takes into account
vim.cmd('redraw')
vim.api.nvim_win_set_cursor(0, {c.cur[1] + 1, c.cur[2]})
if c.left then vim.fn.winrestview({leftcol = c.left}) end
vim.api.nvim_exec_autocmds('CursorMoved', {})
vim.cmd('redraw!')
local info = vim.fn.getwininfo(vim.fn.win_getid())[1]
local rows, attrs = {}, {}
for r = 1, info.height do
  local s, a = {}, {}
  for col = 1, info.width do s[#s + 1] = vim.fn.screenstring(r, col) a[#a + 1] = vim.fn.screenattr(r, col) end
  rows[#rows + 1] = table.concat(s)
  attrs[#attrs + 1] = a
end
-- underlines that start at one place: Neovim draws them in its marktree's
-- order, which follows the B-tree's history (the same two ranges come out
-- either way: measured), so a cell they share accepts any of them
local um, tie = {}, {}
for _, m in ipairs(vim.api.nvim_buf_get_extmarks(0, -1, 0, -1, {details = true})) do
  local s = type(m[4].hl_group) == 'string' and m[4].hl_group:match('^DiagnosticUnderline(%a+)$')
  if s then um[#um + 1] = {m[2], m[3], m[4].end_row, m[4].end_col, ({Error = 1, Warn = 2, Info = 3, Hint = 4})[s]} end
end
local function before(r1, c1, r2, c2) return r1 < r2 or r1 == r2 and c1 < c2 end
for l = info.topline - 1, info.botline - 1 do
  local t = vim.api.nvim_buf_get_lines(0, l, l + 1, false)[1] or ''
  for col = 0, #t - 1 do
    if bit.band(t:byte(col + 1), 0xC0) ~= 0x80 then
      local best, set, n = nil, {}, 0
      for _, m in ipairs(um) do
        if not before(l, col, m[1], m[2]) and before(l, col, m[3], m[4]) then
          if not best or before(best[1], best[2], m[1], m[2]) then best, set, n = m, {}, 0 end
          if m[1] == best[1] and m[2] == best[2] and not set[m[5]] then set[m[5]] = true n = n + 1 end
        end
      end
      local pos = n > 1 and vim.fn.screenpos(0, l + 1, col + 1) or {row = 0}
      if pos.row > 0 then
        local alt = {}
        for k = 1, 4 do if set[k] then alt[#alt + 1] = 'u' .. k end end
        tie[pos.row] = tie[pos.row] or {}
        for x = pos.col, pos.endcol do tie[pos.row][x] = table.concat(alt, '/') end
      end
    end
  end
end
local v = vim.fn.winsaveview()
local p = vim.api.nvim_win_get_cursor(0)
-- each group's attribute alone, from a cell highlighted with it alone
local id, names = {}, {}
for s = 1, 4 do
  local n = ({'Error', 'Warn', 'Info', 'Hint'})[s]
  names[#names + 1] = {'DiagnosticUnderline' .. n, 'u' .. s}
  names[#names + 1] = {'DiagnosticVirtualText' .. n, 'v' .. s}
  names[#names + 1] = {'DiagnosticSign' .. n, 's' .. s}
end
vim.cmd('enew')
vim.api.nvim_buf_set_lines(0, 0, -1, false, {string.rep('x', #names)})
for k, g in ipairs(names) do vim.api.nvim_buf_set_extmark(0, ns, 0, k - 1, {end_col = k, hl_group = g[1]}) end
vim.cmd('redraw!')
local off = vim.fn.getwininfo(vim.fn.win_getid())[1].textoff
for k, g in ipairs(names) do id[vim.fn.screenattr(1, off + k)] = g[2] end
local codes = {}
for r, a in ipairs(attrs) do
  for k, x in ipairs(a) do a[k] = tie[r] and tie[r][k] and (id[x] or ''):match('^u') and tie[r][k] or id[x] or '.' end
  codes[r] = table.concat(a, ' ')
end
io.open(OUT, 'w'):write(vim.json.encode({rows = rows, codes = codes, view = {v.topline - 1, v.leftcol, v.skipcol},
  pos = {p[1] - 1, p[2]}}))
vim.cmd('qa!')
'''

WORDS = ['int', 'x', '=', '1;', 'foo', 'bar(', ')', 'return', '\t', '漢字', 'é', '😀', 'lattice', '--', 'a_b']
MSGS = ['expected ;', 'unused variable', 'no such name', 'type mismatch:\nint vs bool', 'consider a\tTab', '未定义', 'x', 'a\rb', 'crlf\r\nend']

def gen_case(r):
    lines = [' '.join(r.choice(WORDS) for _ in range(r.choice([0, 1, 3, 6, 12, 30])))
             for _ in range(r.randint(1, 30))]
    text = '\n'.join(lines) + '\n'
    b = text.encode()
    n = len(b)
    starts = sorted(r.sample(range(n), min(n, r.choice([0, 1, 2, 4, 8, 16]))))
    diags = []
    for a in starts:
        # never inside a character: the server speaks UTF-16
        while a < n and (b[a] & 0xC0) == 0x80: a += 1
        if a >= n or (diags and diags[-1][0] == a): continue
        z = min(n, a + r.choice([0, 1, 2, 5, 12, 40]))
        while z < n and (b[z] & 0xC0) == 0x80: z += 1
        diags.append([a, z, r.randint(1, 4), r.choice(MSGS)])
    # one at the end of the text: vim.diagnostic puts it on the last line
    if r.random() < 0.1 and (not diags or diags[-1][0] < n): diags.append([n, n, r.randint(1, 4), r.choice(MSGS)])
    # the cursor mostly on a line with a diagnostic, where its message shows
    cur = min(b.count(b'\n', 0, r.choice(diags)[0]), len(lines) - 1) if diags and r.random() < 0.7 else r.randrange(len(lines))
    c = {'text': text, 'diags': diags, 'rows': r.randint(4, 20), 'cols': r.randint(16, 70),
         'wrap': r.random() < 0.5, 'cur': [cur, 0]}
    # on a character's first byte, as the keys keep it
    lb = lines[c['cur'][0]].encode()
    c['cur'][1] = r.choice([k for k in range(len(lb)) if lb[k] & 0xC0 != 0x80] or [0])
    if not c['wrap'] and r.random() < 0.3: c['left'] = r.randint(1, 30)
    return c

def lc(b, p):
    """line and byte column of byte p"""
    l = b.count(b'\n', 0, p)
    return l, p - (b.rfind(b'\n', 0, p) + 1)

def run_nvim(c):
    d = tempfile.mkdtemp()
    open(d + '/f', 'w').write(c['text'])
    b = c['text'].encode()
    nd = []
    for a, z, sev, msg in c['diags']:
        (l, col), (el, ecol) = lc(b, a), lc(b, z)
        nd.append({'lnum': l, 'col': col, 'end_lnum': el, 'end_col': ecol, 'severity': sev, 'message': msg})
    json.dump(dict(c, diags=nd), open(d + '/in.json', 'w'))
    open(d + '/t.lua', 'w').write('local IN, OUT = %r, %r\n' % (d + '/in.json', d + '/out.json') + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', SETUP, '-c', 'luafile ' + d + '/t.lua', 'f'],
                   cwd=d, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    return json.load(open(d + '/out.json'))

def u16(b, p):
    """LSP position (line, UTF-16 units) of byte p"""
    l, col = lc(b, p)
    s = b[p - col:p].decode()
    return {'line': l, 'character': sum(2 if ord(ch) > 0xFFFF else 1 for ch in s)}

def read(path):
    """the recorded cases: text bytes and diagnostics"""
    cases, b, ds = [], b'', []
    for line in open(path):
        if line[0] == 'D':
            a, z, sev, msg = line.split()[1:]
            ds.append((int(a), int(z), int(sev), bytes.fromhex(msg).decode()))
        elif line[0] == 'F': b = bytes.fromhex(line[2:].strip())
        elif line[0] == 'E': cases.append((b, ds)); ds = []
    return cases

def serve(path):
    cases = read(path)
    i, o = sys.stdin.buffer, sys.stdout.buffer
    def send(m):
        s = json.dumps(m).encode()
        o.write(b'Content-Length: %d\r\n\r\n' % len(s) + s)
        o.flush()
    while True:
        n = 0
        while True:
            h = i.readline()
            if not h: return
            if h in (b'\r\n', b'\n'): break
            if h.lower().startswith(b'content-length:'): n = int(h.split(b':')[1])
        m = json.loads(i.read(n))
        if m.get('id') == 0: send({'jsonrpc': '2.0', 'id': 0, 'result': {'capabilities': {}}})
        elif m.get('method') == 'textDocument/didOpen':
            td = m['params']['textDocument']
            k = os.path.basename(td['uri']).split('.')[0]
            if not k.isdigit(): continue
            b, ds = cases[int(k)]
            send({'jsonrpc': '2.0', 'method': 'textDocument/publishDiagnostics', 'params': {
                'uri': td['uri'], 'version': td['version'],
                'diagnostics': [{'range': {'start': u16(b, a), 'end': u16(b, z)}, 'severity': sev, 'message': msg}
                                for a, z, sev, msg in ds]}})

def main():
    a = sys.argv[1:]
    if a[0] == '--serve': return serve(a[1])
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_case(r) for _ in range(n)]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        out = list(ex.map(run_nvim, cases))
    # C rows cols wrap line col left (the cursor and 'leftcol' asked for),
    # D a z sev message-in-hex (a diagnostic), W the view (top line,
    # leftcol, skipcol) and the cursor, F the text in hex, R each window
    # row in hex, A its cells' codes, E
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            f.write('C %d %d %d %d %d %d\n' % (c['rows'], c['cols'], c['wrap'], c['cur'][0], c['cur'][1], c.get('left', 0)))
            for d in c['diags']: f.write('D %d %d %d %s\n' % (d[0], d[1], d[2], d[3].encode().hex()))
            f.write('W %d %d %d %d %d\n' % (tuple(o['view']) + tuple(o['pos'])))
            f.write('F %s\n' % c['text'].encode().hex())
            for row, codes in zip(o['rows'], o['codes']):
                f.write('R %s\nA %s\n' % (row.encode().hex(), codes))
            f.write('E\n')

if __name__ == '__main__':
    main()
