#!/usr/bin/env python3
"""Review of PR 53: random differential test of 'fileformat' against live
nvim --headless (--clean -n, nofixendofline, as tests/test_file_eol.c).

usage: eol_diff.py DRIVER N SEED [edit]

Each case is a random file.  nvim and the driver (eol_drv.c) each open their
own copy, optionally get the same keys, and write.  Compared: 'fileformat',
the lines after opening, the lines after the keys, the read and written
messages (after the file name), and the bytes on disk after the write; with
no keys the bytes must also equal the original file.  The generator is not
the author's: it takes the file as a list of pieces from a table that has
CR LF, LF, lone CR, CR CR LF, LF CR, Ctrl-Z, tabs, UTF-8 and long runs, and
some files are forced all-CR-LF so that about a third are dos.
"""
import json, os, random, subprocess, sys, tempfile

drv, N, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
edit = len(sys.argv) > 4
rnd = random.Random(seed)
PIECES = [b'a', b'b', b' ', b'x y', b'\t', b'\r', b'\r\r', b'\xc3\xa9', b'\xe2\x82\xac', b'\x1a', b'0', b'}']
# only keys ref/keys.c has (package 5 is not built yet)
EDITS = ['', 'ix<Esc>', 'dd', 'Onew<Esc>', 'Gdd', 'Goz<Esc>', 'x', 'ggOq<Esc>', 'Gx', 'ddu', 'ixy<Esc>u', 'ggdd', 'jdd', 'Gox<Esc>ggdd', 'ddGdd', 'Ax<Esc>', 'GAx<Esc>', 'dddddd', 'ddddddix<Esc>']
CODES = {'<Esc>': 27, '<CR>': 13, '<C-v>': 22, '<C-m>': 13}

def gen():
    b = gen1()
    # CTRLZ=0: no Ctrl-Z at all (finding 1 is counted apart)
    return b.replace(b'\x1a', b'z') if os.environ.get('CTRLZ') == '0' else b

def gen1():
    kind = rnd.choice(['unix', 'dos', 'dos', 'mixed', 'mixed'])
    lines = rnd.choice([0, 0, 1, 1, 2, 3, 5, 9, 40])
    out = b''
    for l in range(lines):
        body = b''.join(rnd.choice(PIECES) for _ in range(rnd.choice([0, 0, 1, 2, 4, 9])))
        if rnd.random() < 0.02: body = body * rnd.choice([500, 9000, 40000])   # long lines, past the 64 KiB chunk
        if kind == 'dos' and rnd.random() < 0.9: body = body.replace(b'\x1a', b'z')
        out += body
        if l == lines - 1 and rnd.random() < 0.3: break
        eol = {'unix': b'\n', 'dos': b'\r\n'}.get(kind) or rnd.choice([b'\n', b'\r\n', b'\r\n', b'\r\r\n', b'\n\r'])
        out += eol
    if rnd.random() < 0.1: out += rnd.choice([b'\r', b'\x1a', b'\r\n\x1a', b'\n'])
    return out

def keycodes(ks):
    out, i = [], 0
    while i < len(ks):
        if ks[i] == '<':
            j = ks.index('>', i); out.append(CODES[ks[i:j + 1]]); i = j + 1
        else: out.append(ord(ks[i])); i += 1
    return out

LUA = r'''
local d, n = arg[1], tonumber(arg[2])
vim.o.fixendofline = false
vim.cmd('set expandtab shiftwidth=4 softtabstop=4 autoindent smarttab nostartofline')   -- tools/conform.py's
vim.o.shortmess = 'ltToOC'   -- the default without F, which hides the read message
local function hex(ls) local t = {} for _, l in ipairs(ls) do t[#t + 1] = (l:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end return t end
local keys = vim.json.decode(io.open(d .. '/keys.json'):read('a'))
local res = {}
for i = 0, n - 1 do
  local f = d .. '/n' .. i
  local r = {}
  r.rmsg = vim.api.nvim_exec2('edit! ' .. f, {output = true}).output
  r.ff = vim.bo.fileformat
  r.lines = hex(vim.api.nvim_buf_get_lines(0, 0, -1, true))
  if keys[i + 1] ~= '' then
    vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys[i + 1], true, false, true), 'xt', false)
    r.klines = hex(vim.api.nvim_buf_get_lines(0, 0, -1, true))
  end
  r.wmsg = vim.api.nvim_exec2('write', {output = true}).output
  res[#res + 1] = r
  vim.cmd('bwipeout!')
end
io.open(d .. '/nvim.json', 'w'):write(vim.json.encode(res))
'''

import re
def after_name(m):
    # what follows the file name, up to the counts (nvim may append other
    # messages, such as its regexp engine's, to the same line)
    x = re.search(r'"((?: ?\[[^]]*\])* ?\d+L, \d+B(?: written)?)', m)
    return x.group(1) if x else m.strip()
# the written message without [noeol] is package 4's from before this PR
# (the PR says so under "Not done"): KNOWN=1 compares it without that word
def wr(m):
    m = after_name(m)
    return m.replace('[noeol]', '').replace(' [', '[').replace('[', ' [').replace('  ', ' ') if os.environ.get('KNOWN') else m

with tempfile.TemporaryDirectory() as d:
    files = [gen() for _ in range(N)]
    # CASES=1: the named cases of the review instead of random files, each printed
    if os.environ.get('CASES'):
        files = [b'a\r\nb\nc\r\n', b'a\nb\r\nc\r\n', b'a\r\nb\r\nc\n', b'a\rb\r\nc\r\n', b'a\rb\nc\n', b'a\r\nb\r', b'a\nb\r', b'a\r',
                 b'a\r\nb', b'a\nb', b'a', b'', b'\r\n', b'\n', b'\r', b'\n\r\n', b'\na\r\nb\r\n', b'a\r\r\nb\r\r\n', b'\r\n\r\n',
                 b'a\r\nb\r\n\x1a', b'a\r\nb\x1a', b'a\nb\n\x1a', b'x' * 300000 + b'\r\n' + b'y' * 70000 + b'\r\nz\r\n', b'q\r\n' + b'x' * 300000 + b'\r\n' + b'y' * 70000 + b'\r\n',
                 b'q\n' + b'x' * 300000 + b'\n' + b'y' * 70000]
        N = len(files)
    keys = [rnd.choice(EDITS) if edit else '' for _ in range(N)]
    # the cursor's start column is not what is compared: every sequence starts at line 1, column 0, and G goes to column 0
    keys = ['gg0' + k.replace('G', 'G0') if k else k for k in keys]
    for i, b in enumerate(files):
        for k in 'no': open(f'{d}/{k}{i}', 'wb').write(b)
    json.dump(keys, open(d + '/keys.json', 'w'))
    open(d + '/s.lua', 'w').write(LUA)
    subprocess.run(['nvim', '--headless', '--clean', '-n', '-l', d + '/s.lua', d, str(N)], check=True, stderr=subprocess.DEVNULL)
    nv = json.load(open(d + '/nvim.json'))
    env = dict(os.environ, XDG_STATE_HOME=d + '/state')
    bad, dos, cnt, far = 0, 0, {}, 0
    for i, b in enumerate(files):
        p = subprocess.run([drv, f'{d}/o{i}'], input=' '.join(map(str, keycodes(keys[i]))).encode(), capture_output=True, env=env)
        o = {}
        for ln in p.stdout.decode('utf-8', 'replace').split('\n'):
            w = ln.split(' ', 1)
            if w[0] in ('open', 'keys', 'write') and len(w) > 1:
                if w[1].startswith('lines'): o[w[0] + 'lines'] = [x[:-1] for x in w[1].split(' ')[1:]]
                else: o[w[0]] = w[1]
        n = nv[i]
        # finding 2: nvim takes the format from its first 8,192 bytes; KNOWN=1 counts those files apart
        if os.environ.get('KNOWN') and b.find(b'\n') >= 8192:
            far += 1; far_diff = locals().get('far_diff', 0) + (o.get('open', '').split(' ')[0][3:] != n['ff']); continue
        ours_ff = o.get('open', '').split(' ')[0][3:]
        dos += ours_ff == 'dos'
        diffs = []
        if p.returncode: diffs.append('driver exit %d: %s' % (p.returncode, p.stderr.decode()[-300:]))
        if ours_ff != n['ff']: diffs.append('ff')
        # an empty buffer is one empty line in both
        if o.get('openlines') != (n['lines'] if isinstance(n['lines'], list) else []): diffs.append('lines')
        if keys[i] and o.get('keyslines') != (n.get('klines') if isinstance(n.get('klines'), list) else []): diffs.append('lines after keys')
        if after_name(o.get('open', '').split('msg=', 1)[-1]) != after_name(n['rmsg']): diffs.append('read message')
        wo, wn = wr(o.get('write', '').split('msg=', 1)[-1]), wr(n['wmsg'])
        # a noeol buffer emptied: nvim says 1L, 0B, package 4 says 0L, 0B (before this PR too)
        if os.environ.get('KNOWN'): wn = wn.replace(' 1L, 0B', ' 0L, 0B')
        if wo != wn: diffs.append('written message')
        ob, nb = open(f'{d}/o{i}', 'rb').read(), open(f'{d}/n{i}', 'rb').read()
        if ob != nb: diffs.append('bytes after :w')
        if not keys[i] and ob != b: diffs.append('NOT byte for byte')
        if not keys[i] and nb != b: diffs.append('(nvim not byte for byte)')
        for x in diffs: cnt[x.split(':')[0]] = cnt.get(x.split(':')[0], 0) + 1
        if os.environ.get('CASES'):
            print(f'{b[:24]!r}{"..." if len(b) > 24 else ""} ({len(b)}B) | nvim {n["ff"]} {after_name(n["rmsg"])!r} {len(n["lines"]) if isinstance(n["lines"], list) else 0} lines |{after_name(n["wmsg"])} | ours {ours_ff} {after_name(o.get("open", "").split("msg=", 1)[-1])!r} |{after_name(o.get("write", "").split("msg=", 1)[-1])} | bytes after :w {"same as nvim" if ob == nb else "DIFFER"}, {"the file" if ob == b else "NOT the file"} | {diffs or "equal"}')
        if diffs:
            bad += 1
            if bad <= int(os.environ.get('SHOW', 8)):
                print(f'case {i} keys {keys[i]!r} file {b[:60]!r}{"..." if len(b) > 60 else ""} ({len(b)} bytes): {diffs}')
                print('   ours:', o.get('open', '')[:150].replace(d, ''), '|', o.get('write', '')[:150].replace(d, ''))
                print('   nvim:', n['ff'], after_name(n['rmsg']), '|', after_name(n['wmsg']))
                if 'lines' in diffs: print('   lines ours', [x[:40] for x in o.get('openlines', [])[-4:]], 'nvim', [x[:40] for x in n['lines'][-4:]] if isinstance(n['lines'], list) else n['lines'])
                if 'bytes after :w' in diffs: print('   bytes ours', ob[-40:], 'nvim', nb[-40:])
    print(f'{N} files (seed {seed}, {dos} dos, {sum(len(f) > 65536 for f in files)} over 64 KiB{", with keys" if edit else ""}): {bad} differ {cnt}' + (f'; {far} files with the first LF past byte 8,192 set aside, {far_diff} of them with another format than nvim' if far else ''))
    sys.exit(1 if bad else 0)
