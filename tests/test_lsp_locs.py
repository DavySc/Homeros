#!/usr/bin/env python3
"""Records what Neovim 0.12 makes of a language server's places and
diagnostics, for test_lsp_locs.c.
usage: test_lsp_locs.py N [SEED] --record FILE
       test_lsp_locs.py --serve FILE LOG   (the fake language server)
A case is one JSON line.  "loc": files (some not UTF-8 clean, CR LF, past
their end), a method and the server's answer (Location, Location[],
LocationLink[], DocumentSymbol[] nested, SymbolInformation[], null, an
"error"), URIs under "ROOT/" (some of a file never written), which the
server replaces with the case's directory; "items" is
vim.lsp.util.locations_to_items() (for a definition, each place alone,
in the server's order, as vim.lsp.tagfunc) or symbols_to_items() with
the main file m.c loaded, as [file, line, col, text] (file relative to
the case's directory, line and col from 1); after the random ones, a few
places the key cannot reach (edges()).  Then the method's key from
Neovim's defaults ("key": C-], grr, gri, grt, gO, and Davy's gd:
vim.lsp.buf.definition()) typed at "cur" with
this file as the server: "req" what it was asked, "msg" what Neovim said
last, then "list" the quickfix or location list's [file, line, col,
text] (file relative to the current directory) and "row" the cursor in
it, or "at" the file, line and column after the key and "back" after
C-o; the first such case has "caps", what the client said it takes.
"diag": a text, diagnostics [a, z) as lsp.h keeps them, the cursor, keys
from Neovim's default maps ("]d" "[d" with a count, "]D" "[D"); "pos"
the cursor after them, "back" after C-o and "more" 0 when Neovim said
"No more valid diagnostics to move to".  As the server it answers any
request about case k's m.c with its answer (logging the request, with
the case's directory as ROOT/, to LOG, and on initialize what the client
takes to LOG.init) and publishes its diagnostics when m.c is opened;
clangd's textDocument/switchSourceHeader gets the case's o.c."""
import concurrent.futures, json, os, random, subprocess, sys, tempfile, urllib.parse

LUA = r'''
local out = io.open(OUT, 'w')
for line in io.lines(IN) do
  local c = vim.json.decode(line)
  local dir = ROOT .. '/' .. c.k
  vim.cmd('silent edit ' .. vim.fn.fnameescape(dir .. '/m.c'))
  local buf = vim.api.nvim_get_current_buf()
  local r = {}
  if c.kind == 'loc' then
    local res = vim.json.decode((vim.json.encode(c.result):gsub('ROOT/', vim.uri_from_fname(dir) .. '/')), {luanil = {object = true, array = true}})
    local items
    if type(res) ~= 'table' or vim.tbl_isempty(res) then items = {}
    elseif (vim.islist(res) and res[1] or res).name then items = vim.lsp.util.symbols_to_items(res, buf, 'utf-16')
    elseif c.method == 'textDocument/references' then items = vim.lsp.util.locations_to_items(res, 'utf-16')
    elseif c.method ~= 'textDocument/definition' then items = vim.lsp.util.locations_to_items(vim.islist(res) and res or {res}, 'utf-16')
    else   -- in the server's order, as vim.lsp.tagfunc takes them
      items = {}
      for _, l in ipairs(vim.islist(res) and res or {res}) do items[#items + 1] = vim.lsp.util.locations_to_items({l}, 'utf-16')[1] end
    end
    r.items = {}
    for _, it in ipairs(items) do
      r.items[#r.items + 1] = {it.filename:sub(#dir + 2), it.lnum, it.col, it.text}
    end
    -- the key, against this file as the server (it logs what it is asked),
    -- named as the core names one: its program's name
    if client then vim.lsp.buf_attach_client(buf, client)
    else
      vim.keymap.set('n', 'gd', vim.lsp.buf.definition)   -- Davy's
      client = vim.lsp.start({ name = 'python3', cmd = { 'python3', PY, '--serve', IN, LOG }, root_dir = ROOT }) end
    vim.wait(5000, function() return vim.lsp.buf_is_attached(buf, client) and vim.lsp.get_client_by_id(client).initialized end)
    if not caps then caps = vim.fn.readfile(LOG .. '.init')[1]; r.caps = caps end
    vim.api.nvim_win_set_cursor(0, {c.cur[1] + 1, c.cur[2]})
    vim.cmd('messages clear | clearjumps')
    local n0 = #vim.fn.readfile(LOG)
    local ok, err = pcall(vim.api.nvim_feedkeys, vim.api.nvim_replace_termcodes(c.key, true, false, true), 'x', false)
    vim.wait(5000, function() return next(vim.lsp.get_client_by_id(client).requests) == nil end)
    vim.wait(20)
    r.req = {}
    for i, l in ipairs(vim.fn.readfile(LOG)) do if i > n0 then r.req[#r.req + 1] = l end end
    local here = function() local p = vim.api.nvim_win_get_cursor(0) return {vim.fn.fnamemodify(vim.fn.bufname(), ':.'), p[1] - 1, p[2]} end
    local ms = vim.trim(vim.fn.execute('messages'))
    -- a Lua error's own words, without where and its traceback
    if ms:find('stack traceback', 1, true) then ms = ms:match('^[^\n]*'):match('.*:%d+: (.*)$') end
    r.msg = (ok and ms:match('[^\n]*$') or tostring(err):match('E%d+:.*')):gsub(vim.pesc(ROOT .. '/'), '')
    if vim.bo.buftype == 'quickfix' then
      local loc = vim.fn.getwininfo(vim.api.nvim_get_current_win())[1].loclist == 1
      r.list = {}
      for _, it in ipairs(loc and vim.fn.getloclist(0) or vim.fn.getqflist()) do
        r.list[#r.list + 1] = {vim.fn.fnamemodify(vim.fn.bufname(it.bufnr), ':.'), it.lnum, it.col, it.text}
      end
      r.row = vim.api.nvim_win_get_cursor(0)[1] - 1
      vim.cmd('cclose | lclose')
    else
      r.at = here()
      vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes('<C-o>', true, false, true), 'x', false)
      r.back = here()
    end
  else
    local ns = vim.api.nvim_create_namespace('t')
    vim.diagnostic.reset(ns, buf)
    vim.diagnostic.set(ns, buf, c.nd)
    vim.api.nvim_win_set_cursor(0, {c.cur[1] + 1, c.cur[2]})
    vim.cmd('messages clear | clearjumps')
    vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(c.keys, true, false, true), 'x', false)
    local p = vim.api.nvim_win_get_cursor(0)
    r.pos = {p[1] - 1, p[2]}
    r.more = vim.fn.execute('messages'):find('No more valid diagnostics', 1, true) and 0 or 1
    vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes('<C-o>', true, false, true), 'x', false)
    p = vim.api.nvim_win_get_cursor(0)
    r.back = {p[1] - 1, p[2]}
  end
  out:write(vim.json.encode(r) .. '\n')
end
out:close()
vim.cmd('qa!')
'''

WORDS = ['int', 'x', '=', '1;', 'foo', 'bar(', ')', '\t', '漢字', 'é', '😀', 'a_b', '  ']
KINDS = list(range(0, 28))

def text(r):
    lines = [' '.join(r.choice(WORDS) for _ in range(r.choice([0, 1, 3, 6, 12])))
             for _ in range(r.randint(1, 12))]
    eol = '\r\n' if r.random() < 0.15 else '\n'
    return eol.join(lines) + ('' if r.random() < 0.1 else eol)

def pos(r, t):
    """an LSP position in text t, sometimes past a line's end or the last line,
    sometimes inside a surrogate pair"""
    lines = t.split('\n')
    l = r.randrange(len(lines) + (2 if r.random() < 0.1 else 0))
    s = lines[l] if l < len(lines) else ''
    u = sum(2 if ord(ch) > 0xFFFF else 1 for ch in s)
    return {'line': l, 'character': r.randint(0, u + (3 if r.random() < 0.1 else 0))}

def rng(r, t):
    a, b = pos(r, t), pos(r, t)
    if (b['line'], b['character']) < (a['line'], a['character']): a, b = b, a
    return {'start': a, 'end': b}

NAMES = ['m.c', 'o.c', 'sub dir/ö.h', 'a%b.h', 'z.h']
GONE = 'gone.h'   # named in answers, never written

def symbol(r, t, depth):
    s = {'name': r.choice(['add', 'pt', 'x', 'λ', 'main']), 'kind': r.choice(KINDS), 'range': rng(r, t)}
    s['selectionRange'] = rng(r, t)
    if r.random() < 0.3: s['detail'] = 'int (int)'
    if r.random() < 0.1: s['deprecated'] = True
    if r.random() < 0.1: s['tags'] = [1]
    if depth < 2 and r.random() < 0.4: s['children'] = [symbol(r, t, depth + 1) for _ in range(r.randint(0, 3))]
    return s

def gen_loc(r, k):
    files = {n: text(r) for n in NAMES}
    uri = lambda n: 'ROOT/' + urllib.parse.quote(n)
    shape = r.choice(['loc1', 'locs', 'links', 'docsym', 'syminfo', 'null', 'empty', 'error'])
    name = lambda: GONE if r.random() < 0.05 else r.choice(NAMES)
    if shape in ('loc1',):
        n = name(); res = {'uri': uri(n), 'range': rng(r, files.get(n, ''))}
    elif shape == 'locs':
        res = []
        for _ in range(r.randint(1, 8)):
            n = name(); res.append({'uri': uri(n), 'range': rng(r, files.get(n, ''))})
    elif shape == 'links':
        res = []
        for _ in range(r.randint(1, 5)):
            n = r.choice(NAMES)
            res.append({'targetUri': uri(n), 'targetRange': rng(r, files[n]), 'targetSelectionRange': rng(r, files[n])})
    elif shape == 'docsym': res = [symbol(r, files['m.c'], 0) for _ in range(r.randint(1, 5))]
    elif shape == 'syminfo':
        res = []
        for _ in range(r.randint(1, 5)):
            n = 'm.c'   # Neovim takes the columns of any file's from the current buffer
            s = {'name': r.choice(['add', 'twice']), 'kind': r.choice(KINDS), 'location': {'uri': uri(n), 'range': rng(r, files[n])}}
            if r.random() < 0.5: s['containerName'] = r.choice(['', 'pt', 'ns'])
            if r.random() < 0.1: s['deprecated'] = True
            res.append(s)
    elif shape in ('null', 'error'): res = None
    else: res = []
    method = {'docsym': 'textDocument/documentSymbol', 'syminfo': 'textDocument/documentSymbol'}.get(
        shape, r.choice(['textDocument/definition', 'textDocument/references', 'textDocument/implementation', 'textDocument/typeDefinition']
                        + ['textDocument/documentSymbol'] * (shape in ('null', 'empty', 'error'))))
    key = {'textDocument/definition': r.choice(['<C-]>', 'gd']), 'textDocument/references': 'grr', 'textDocument/implementation': 'gri',
           'textDocument/typeDefinition': 'grt', 'textDocument/documentSymbol': 'gO'}[method]
    lines = files['m.c'].replace('\r\n', '\n').split('\n')
    cl = r.randrange(len(lines) - (len(lines) > 1 and lines[-1] == ''))
    lb = lines[cl].encode()
    cc = r.choice([i for i in range(len(lb)) if lb[i] & 0xC0 != 0x80] or [0])
    c = {'k': k, 'kind': 'loc', 'files': files, 'method': method, 'result': res, 'key': key, 'cur': [cl, cc]}
    if shape == 'error': c['error'] = 'boom'   # the server's answer an error
    return c

def lc(b, p):
    l = b.count(b'\n', 0, p)
    return l, p - (b.rfind(b'\n', 0, p) + 1)

def gen_diag(r, k):
    t = text(r).replace('\r\n', '\n')
    b = t.encode()
    n = len(b)
    ds = []
    for a in sorted(r.sample(range(n + 1), min(n + 1, r.choice([0, 1, 2, 3, 6])))):
        while a < n and (b[a] & 0xC0) == 0x80: a += 1
        if ds and ds[-1][0] == a: continue
        z = min(n, a + r.choice([0, 1, 4]))
        while z < n and (b[z] & 0xC0) == 0x80: z += 1
        ds.append([a, z])
    lines = t.split('\n')
    if t.endswith('\n'): lines.pop()
    cl = r.randrange(len(lines))
    lb = lines[cl].encode()
    cc = r.choice([i for i in range(len(lb)) if lb[i] & 0xC0 != 0x80] or [0])
    keys = r.choice(['', '2', '3']) + r.choice([']d', '[d']) if r.random() < 0.7 else r.choice([']D', '[D'])
    # "[D" where a diagnostic sits past its line's last character: each
    # step starts from its raw column, after itself, so Neovim takes it
    # again 2^31 times, a hang; the core stops there
    def moved(a):
        l, c = lc(b, a)
        return c > max(len(lines[min(l, len(lines) - 1)].encode()) - 1, 0)
    if keys == '[D' and any(moved(a) for a, z in ds): keys = '[d'
    nd = []
    for a, z in ds:
        (l, c), (el, ec) = lc(b, a), lc(b, z)
        nd.append({'lnum': l, 'col': c, 'end_lnum': el, 'end_col': ec, 'severity': 1, 'message': 'm'})
    return {'k': k, 'kind': 'diag', 'files': {'m.c': t}, 'diags': ds, 'nd': nd, 'cur': [cl, cc], 'keys': keys}

def run_nvim(cases):
    d = tempfile.mkdtemp()
    for c in cases:
        for n, t in c['files'].items():
            p = os.path.join(d, str(c['k']), n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(t.encode())
    with open(d + '/in.jsonl', 'w') as f:
        for c in cases: f.write(json.dumps(c) + '\n')
    open(d + '/log', 'w').close()
    open(d + '/t.lua', 'w').write('local IN, OUT, ROOT, PY, LOG, client, caps = %r, %r, %r, %r, %r\n'
                                  % (d + '/in.jsonl', d + '/out.jsonl', d, os.path.abspath(__file__), d + '/log') + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', 'set noswapfile nomore hidden', '-c', 'luafile ' + d + '/t.lua'],
                   cwd=d, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, timeout=600)
    return [json.loads(l) for l in open(d + '/out.jsonl')]

def u16(b, p):
    l, col = lc(b, p)
    s = b[p - col:p].decode()
    return {'line': l, 'character': sum(2 if ord(ch) > 0xFFFF else 1 for ch in s)}

def serve(path, log):
    cases = [json.loads(l) for l in open(path)]
    i, o = sys.stdin.buffer, sys.stdout.buffer
    def send(m):
        s = json.dumps(m).encode()
        o.write(b'Content-Length: %d\r\n\r\n' % len(s) + s)
        o.flush()
    def case(uri):
        u = uri.rsplit('/', 2)
        return cases[int(u[-2])], u[0] + '/' + u[1] + '/'
    while True:
        n = 0
        while True:
            h = i.readline()
            if not h: return
            if h in (b'\r\n', b'\n'): break
            if h.lower().startswith(b'content-length:'): n = int(h.split(b':')[1])
        m = json.loads(i.read(n))
        p = m.get('params') or {}
        if m.get('method') == 'initialize':
            # what the client says it takes back: a Location for LocationLink
            t = p['capabilities'].get('textDocument', {})
            open(log + '.init', 'w').write(json.dumps({k: t.get(k, {}).get(v) for k, v in [('definition', 'linkSupport'),
                ('implementation', 'linkSupport'), ('typeDefinition', 'linkSupport'), ('documentSymbol', 'hierarchicalDocumentSymbolSupport')]},
                sort_keys=True) + '\n')
            send({'jsonrpc': '2.0', 'id': m['id'], 'result': {'capabilities': {k: True for k in (
                'definitionProvider', 'referencesProvider', 'implementationProvider', 'typeDefinitionProvider', 'documentSymbolProvider')}}})
        elif m.get('method') == 'textDocument/didOpen':
            td = p['textDocument']
            c, _ = case(td['uri'])
            if c['kind'] == 'diag':
                b = c['files']['m.c'].encode()
                send({'jsonrpc': '2.0', 'method': 'textDocument/publishDiagnostics', 'params': {
                    'uri': td['uri'], 'version': td['version'],
                    'diagnostics': [{'range': {'start': u16(b, a), 'end': u16(b, z)}, 'severity': 1, 'message': 'm'}
                                    for a, z in c['diags']]}})
        elif 'id' in m and 'method' in m:
            c, root = case((p.get('textDocument') or p)['uri'])
            with open(log, 'a') as f: f.write(json.dumps([m['method'], p], sort_keys=True).replace(root, 'ROOT/') + '\n')
            # clangd's: the other file, a URI
            res = root + 'o.c' if m['method'] == 'textDocument/switchSourceHeader' else \
                json.loads(json.dumps(c.get('result')).replace('ROOT/', root))
            send({'jsonrpc': '2.0', 'id': m['id'], 'error': {'code': -32603, 'message': c['error']}} if 'error' in c else
                 {'jsonrpc': '2.0', 'id': m['id'], 'result': res})

def edges(k):
    """A place the key cannot reach, rare at random: C-] past the file's end
    ("^name\\s*(" guessed, ignoring case as tag.c does; nothing guessed, in
    another file whose first line is blank or indented, and in this one), in
    a file that is not there, and gri past the end"""
    def at(f, line): return {'uri': 'ROOT/' + f, 'range': {'start': {'line': line, 'character': 0}, 'end': {'line': line, 'character': 0}}}
    e = [('<C-]>', 'x = bar(1);\n', [0, 5], 'x\nbar (int a)\n', at('o.c', 9)),
         ('<C-]>', 'x = bar(1);\n', [0, 5], 'x\nint BAR(void)\n', at('o.c', 9)),
         ('<C-]>', 'x = bar(1);\n', [0, 5], '  \nint y;\n', at('o.c', 9)),
         ('<C-]>', 'x = bar(1);\n', [0, 5], '\tint y;\nz\n', at('o.c', 9)),
         ('<C-]>', 'x\n = baz;\n', [1, 3], '', at('m.c', 9)),
         ('<C-]>', 'x = bar(1);\n', [0, 5], '', at(GONE, 0)),
         ('gri', 'x = bar(1);\n', [0, 5], 'x\n', at('o.c', 3))]
    method = {'<C-]>': 'textDocument/definition', 'gri': 'textDocument/implementation'}
    return [{'k': k + i, 'kind': 'loc', 'files': {'m.c': m, 'o.c': o}, 'method': method[key], 'result': res, 'key': key, 'cur': cur}
            for i, (key, m, cur, o, res) in enumerate(e)]

def main():
    a = sys.argv[1:]
    if a[0] == '--serve': return serve(a[1], a[2])
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_loc(r, k) if k % 2 else gen_diag(r, k) for k in range(n)]
    cases += edges(n)
    out = run_nvim(cases)
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            c.pop('nd', None)
            c.update(o)
            f.write(json.dumps(c, ensure_ascii=False) + '\n')

if __name__ == '__main__':
    main()
