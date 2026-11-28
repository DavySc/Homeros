#!/usr/bin/env python3
"""A fake server for hold.c: answers semanticTokens/full with a token on
every word of LINES lines of WORDS words "x " each.  MODE error: answers
the N-th token request (argv 4) and later with an error instead."""
import json, sys
lines, words = int(sys.argv[1]), int(sys.argv[2])
errfrom = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
data = []
for l in range(lines):
    for w in range(words):
        data += [1 if (w == 0 and l) else 0, 0 if w == 0 else 2, 1, w % 2, 0]
body = ','.join(map(str, data))
inp, out, nreq = sys.stdin.buffer, sys.stdout.buffer, 0
def send(s):
    b = s.encode(); out.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); out.flush()
while True:
    n = 0
    while True:
        h = inp.readline()
        if not h: sys.exit(0)
        if h in (b'\r\n', b'\n'): break
        if h.lower().startswith(b'content-length'): n = int(h.split(b':')[1])
    m = json.loads(inp.read(n))
    if m.get('method') == 'initialize':
        send(json.dumps({'jsonrpc': '2.0', 'id': m['id'], 'result': {'capabilities': {'textDocumentSync': 2,
             'semanticTokensProvider': {'legend': {'tokenTypes': ['keyword', 'variable'], 'tokenModifiers': []}, 'full': True}}}}))
    elif m.get('method') == 'textDocument/semanticTokens/full':
        nreq += 1
        if nreq >= errfrom: send('{"jsonrpc":"2.0","id":%d,"error":{"code":-32801,"message":"content modified"}}' % m['id'])
        else: send('{"jsonrpc":"2.0","id":%d,"result":{"data":[%s]}}' % (m['id'], body))
    elif 'id' in m and 'method' in m:
        send('{"jsonrpc":"2.0","id":%d,"result":null}' % m['id'])
