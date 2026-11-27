#!/usr/bin/env python3
"""Real `lean --server` on a long file: is the answer to a
semanticTokens/full request asked right after didOpen (as the PR asks) the
whole file's tokens?  Asks once at open, then again after the file has
elaborated, and prints the count and last line of each answer.
leanpartial.py FILE [range]"""
import json, os, select, subprocess, sys, time
path = os.path.abspath(sys.argv[1]); text = open(path).read()
p = subprocess.Popen(['lean', '--server'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=os.path.dirname(path))
buf = b''
def send(o):
    b = json.dumps(o).encode(); p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
def recv(t):
    global buf
    end = time.time() + t
    while True:
        if b'\r\n\r\n' in buf:
            h, rest = buf.split(b'\r\n\r\n', 1); n = int(h.split(b':')[1])
            if len(rest) >= n: buf = rest[n:]; return json.loads(rest[:n])
        r, _, _ = select.select([p.stdout], [], [], max(0, end - time.time()))
        if not r: return None
        buf += os.read(p.stdout.fileno(), 1 << 20)
uri = 'file://' + path
send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': None, 'rootUri': None, 'capabilities': {'workspace': {'semanticTokens': {'refreshSupport': True}}}}})
while (m := recv(60)) and m.get('id') != 0: pass
send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'lean4', 'version': 1, 'text': text}}})
t0 = time.time(); nlines = text.count('\n')
def ask(i): send({'jsonrpc': '2.0', 'id': i, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
ask(5)
done = None; asked2 = False; got = 0
while got < 2:
    m = recv(120)
    if m is None: print('silence for 120 s'); break
    if 'id' in m and 'method' in m:
        print('  %5.0f ms: the server asks %s' % ((time.time() - t0) * 1000, m['method']))
        send({'jsonrpc': '2.0', 'id': m['id'], 'result': None}); continue
    if m.get('method') == '$/lean/fileProgress' and not m['params']['processing'] and not asked2:
        done = time.time(); print('  %5.0f ms: the file is elaborated (fileProgress empty)' % ((done - t0) * 1000)); asked2 = True; ask(6)
    if m.get('id') in (5, 6):
        d = m['result']['data']; l = 0; size = len(json.dumps(m))
        for i in range(0, len(d), 5): l += d[i]
        print('  %5.0f ms: answer to the request %s: %d tokens, the last on line %d of %d, %d bytes' % ((time.time() - t0) * 1000, 'at open' if m['id'] == 5 else 'after elaboration', len(d) // 5, l + 1, nlines, size)); got += 1
p.kill()
