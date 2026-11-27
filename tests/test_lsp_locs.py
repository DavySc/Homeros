#!/usr/bin/env python3
"""Records what Neovim 0.12 makes of a language server's places and
diagnostics, for test_lsp_locs.c.
usage: test_lsp_locs.py N [SEED] --record FILE
       test_lsp_locs.py --serve FILE   (the fake language server)
A case is one JSON line.  "loc": files (some not UTF-8 clean, CR LF, past
their end), a method and the server's answer (Location, Location[],
LocationLink[], DocumentSymbol[] nested, SymbolInformation[], null),
URIs under "ROOT/", which the server replaces with the case's directory;
"items" is vim.lsp.util.locations_to_items() or symbols_to_items() with
the main file m.c loaded, as [file, line, col, text] (file relative to
the case's directory, line and col from 1).  "diag": a text, diagnostics
[a, z) as lsp.h keeps them, the cursor, keys from Neovim's default maps
("]d" "[d" with a count, "]D" "[D"); "pos" the cursor after them and
"more" 0 when Neovim said "No more valid diagnostics to move to".  As the
server it answers any request about case k's m.c with its answer and
publishes its diagnostics when m.c is opened; clangd's
textDocument/switchSourceHeader gets the case's o.c."""
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
    else items = vim.lsp.util.locations_to_items(vim.islist(res) and res or {res}, 'utf-16') end
    r.items = {}
    for _, it in ipairs(items) do
      r.items[#r.items + 1] = {it.filename:sub(#dir + 2), it.lnum, it.col, it.text}
    end
  else
    local ns = vim.api.nvim_create_namespace('t')
    vim.diagnostic.reset(ns, buf)
    vim.diagnostic.set(ns, buf, c.nd)
    vim.api.nvim_win_set_cursor(0, {c.cur[1] + 1, c.cur[2]})
    vim.cmd('messages clear')
    vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(c.keys, true, false, true), 'x', false)
    local p = vim.api.nvim_win_get_cursor(0)
    r.pos = {p[1] - 1, p[2]}
    r.more = vim.fn.execute('messages'):find('No more valid diagnostics', 1, true) and 0 or 1
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
    shape = r.choice(['loc1', 'locs', 'links', 'docsym', 'syminfo', 'null', 'empty'])
    if shape == 'loc1': res = {'uri': uri(r.choice(NAMES)), 'range': None}
    if shape in ('loc1',):
        n = r.choice(NAMES); res = {'uri': uri(n), 'range': rng(r, files[n])}
    elif shape == 'locs':
        res = []
        for _ in range(r.randint(1, 8)):
            n = r.choice(NAMES); res.append({'uri': uri(n), 'range': rng(r, files[n])})
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
    elif shape == 'null': res = None
    else: res = []
    method = {'docsym': 'textDocument/documentSymbol', 'syminfo': 'textDocument/documentSymbol'}.get(
        shape, r.choice(['textDocument/definition', 'textDocument/references', 'textDocument/implementation']))
    return {'k': k, 'kind': 'loc', 'files': files, 'method': method, 'result': res}

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
    open(d + '/t.lua', 'w').write('local IN, OUT, ROOT = %r, %r, %r\n' % (d + '/in.jsonl', d + '/out.jsonl', d) + LUA)
    subprocess.run(['nvim', '--clean', '--headless', '-c', 'set noswapfile nomore hidden', '-c', 'luafile ' + d + '/t.lua'],
                   cwd=d, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, timeout=600)
    return [json.loads(l) for l in open(d + '/out.jsonl')]

def u16(b, p):
    l, col = lc(b, p)
    s = b[p - col:p].decode()
    return {'line': l, 'character': sum(2 if ord(ch) > 0xFFFF else 1 for ch in s)}

def serve(path):
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
        if m.get('id') == 0: send({'jsonrpc': '2.0', 'id': 0, 'result': {'capabilities': {}}})
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
            # clangd's: the other file, a URI
            res = root + 'o.c' if m['method'] == 'textDocument/switchSourceHeader' else \
                json.loads(json.dumps(c.get('result')).replace('ROOT/', root))
            send({'jsonrpc': '2.0', 'id': m['id'], 'result': res})

def main():
    a = sys.argv[1:]
    if a[0] == '--serve': return serve(a[1])
    n, seed = int(a[0]), int(a[1]) if len(a) > 1 and a[1][0] != '-' else 1
    r = random.Random(seed)
    cases = [gen_loc(r, k) if k % 2 else gen_diag(r, k) for k in range(n)]
    out = run_nvim(cases)
    with open(a[a.index('--record') + 1], 'w') as f:
        for c, o in zip(cases, out):
            c.pop('nd', None)
            c.update(o)
            f.write(json.dumps(c, ensure_ascii=False) + '\n')

if __name__ == '__main__':
    main()
