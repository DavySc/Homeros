#!/usr/bin/env python3
"""Real `lean --server`: what answers a semanticTokens/full request gets
(a) after a header change (the file worker restarts), asked right after the
change as the PR does, (b) for a file whose import does not exist."""
import json, os, select, subprocess, sys, time, tempfile
def session(text, edits):
    d = tempfile.mkdtemp(); path = os.path.join(d, 'A.lean'); open(path, 'w').write(text)
    p = subprocess.Popen(['lean', '--server'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=d)
    buf = [b'']
    def send(o):
        b = json.dumps(o).encode(); p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
    def recv(t):
        end = time.time() + t
        while True:
            if b'\r\n\r\n' in buf[0]:
                h, rest = buf[0].split(b'\r\n\r\n', 1); n = int(h.split(b':')[1])
                if len(rest) >= n: buf[0] = rest[n:]; return json.loads(rest[:n])
            r, _, _ = select.select([p.stdout], [], [], max(0, end - time.time()))
            if not r: return None
            buf[0] += os.read(p.stdout.fileno(), 1 << 20)
    uri = 'file://' + path
    send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': None, 'rootUri': None, 'capabilities': {}}})
    while (m := recv(60)) and m.get('id') != 0: pass
    send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
    send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'lean4', 'version': 1, 'text': text}}})
    nid, ver, last = 10, 1, None
    def ask():
        nonlocal nid, last
        if last: send({'jsonrpc': '2.0', 'method': '$/cancelRequest', 'params': {'id': last}})
        nid += 1; last = nid
        send({'jsonrpc': '2.0', 'id': nid, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
    ask()
    t0 = time.time()
    def drain(t):
        end = time.time() + t
        while time.time() < end:
            m = recv(end - time.time())
            if not m: continue
            if 'id' in m and 'method' in m: send({'jsonrpc': '2.0', 'id': m['id'], 'result': None}); continue
            if 'id' in m:
                what = 'error ' + json.dumps(m['error']) if 'error' in m else '%d tokens' % (len(m['result']['data']) // 5)
                print('  %5.0f ms: answer to %d%s: %s' % ((time.time() - t0) * 1000, m['id'], ' (the latest)' if m['id'] == last else ' (cancelled)', what[:160]))
    drain(4)
    for (l, c, s) in edits:
        ver += 1
        send({'jsonrpc': '2.0', 'method': 'textDocument/didChange', 'params': {'textDocument': {'uri': uri, 'version': ver},
              'contentChanges': [{'range': {'start': {'line': l, 'character': c}, 'end': {'line': l, 'character': c}}, 'text': s}]}})
        ask()
        print('  %5.0f ms: change %r at %d:%d, asked %d' % ((time.time() - t0) * 1000, s, l, c, last))
        drain(0.15)
    drain(15)
    p.kill()
body = 'def f (n : Nat) : Nat := n + 1\ntheorem t (a : Nat) : f a = a + 1 := by\n  rfl\n'
print('a header change, then typing in the body:'); session('\n' + body, [(0, 0, 'import Lean\n')] + [(2, 4 + i, ' ') for i in range(3)])
print('a header change alone:'); session('\n' + body, [(0, 0, 'import Lean\n')])
print('an import that does not exist:'); session('import Nope\n' + body, [])
print('typing in the header (i m p o r t ...):'); session('\n' + body, [(0, i, ch) for i, ch in enumerate('import Lean')])
