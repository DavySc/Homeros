#!/usr/bin/env python3
"""Asks real `lean --server` for the semantic tokens of a file and prints
the legend and each token with its text.  leantok.py FILE [caps-json]"""
import json, os, select, subprocess, sys, time
path = os.path.abspath(sys.argv[1]); text = open(path).read()
p = subprocess.Popen(['lean', '--server'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=os.path.dirname(path))
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
send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': None, 'rootUri': None, 'capabilities': {'textDocument': {'semanticTokens': {'requests': {'full': True}, 'tokenTypes': [], 'tokenModifiers': [], 'formats': ['relative']}}}}})
while (m := recv(60)) and m.get('id') != 0: pass
leg = m['result']['capabilities']['semanticTokensProvider']
print('provider:', json.dumps(leg))
types = leg['legend']['tokenTypes']
send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'lean4', 'version': 1, 'text': text}}})
t0 = time.time()
send({'jsonrpc': '2.0', 'id': 5, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
while True:
    m = recv(300)
    if m is None: print('no answer'); sys.exit(1)
    if 'id' in m and 'method' in m: send({'jsonrpc': '2.0', 'id': m['id'], 'result': None})
    if m.get('id') == 5 and 'method' not in m: break
print('answer after %.0f ms' % ((time.time() - t0) * 1000), 'keys', list(m.get('result', m).keys()))
d = m['result']['data']; L = text.split('\n'); l = c = 0; prev_end = (0, 0); used = {}
def u16(s, n):   # utf-16 col -> python index
    i = u = 0
    while i < len(s) and u < n: u += 2 if ord(s[i]) > 0xFFFF else 1; i += 1
    return i
print(len(d) // 5, 'tokens')
for i in range(0, len(d), 5):
    dl, dc, ln, ty, mod = d[i:i + 5]
    if dl: l += dl; c = dc
    else: c += dc
    if (l, c) < prev_end: print('OVERLAP/UNSORTED at', l, c)
    prev_end = (l, c + ln)
    w = L[l][u16(L[l], c):u16(L[l], c + ln)]
    used[types[ty]] = used.get(types[ty], 0) + 1
    if '-q' not in sys.argv: print(f'{l + 1}:{c} {types[ty]:12} mod={mod} {w!r}' + ('  PAST LINE END' if c + ln > len(L[l].encode('utf-16-le')) // 2 else ''))
print('used:', used)
p.kill()
