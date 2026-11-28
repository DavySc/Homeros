#!/usr/bin/env python3
"""A fake server for many.c: N files (argv 1).  The first answer for a
file colours its first word only (Lean, before it has elaborated the rest);
once every file has had one, a single workspace/semanticTokens/refresh
goes out; later answers colour both words, or carry $MANY_DATA as
their "data".  Each request is logged to
stderr."""
import json, os, sys
want = int(sys.argv[1])
inp, out, asked, sent = sys.stdin.buffer, sys.stdout.buffer, {}, False
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
             'semanticTokensProvider': {'legend': {'tokenTypes': ['keyword'], 'tokenModifiers': []}, 'full': True}}}}))
    elif m.get('method') == 'textDocument/semanticTokens/full':
        u = m['params']['textDocument']['uri']
        asked[u] = asked.get(u, 0) + 1
        send('{"jsonrpc":"2.0","id":%d,"result":{"data":%s}}' % (m['id'], '[0,0,1,0,0]' if asked[u] == 1 else os.environ.get('MANY_DATA', '[0,0,1,0,0,0,2,1,0,0]')))
        if len(asked) == want and not sent:
            sent = True
            send('{"jsonrpc":"2.0","id":9000,"method":"workspace/semanticTokens/refresh"}')
    elif 'id' in m and 'method' in m:
        send('{"jsonrpc":"2.0","id":%d,"result":null}' % m['id'])
