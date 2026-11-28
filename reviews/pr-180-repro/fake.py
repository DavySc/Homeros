#!/usr/bin/env python3
"""A fake language server for the PR 180 probes.  usage: fake.py LOG
hover at line L of the file: line 0 -> Markdown "ANSWER FOR LINE 0" with a
C code block; line 1 -> null; line 2 -> a long clangd-like answer.
Logs every method it receives (and a cancel's id) to LOG."""
import json, sys
log = open(sys.argv[1], 'a')
def send(m):
    b = json.dumps(m).encode()
    sys.stdout.buffer.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); sys.stdout.buffer.flush()
CLANGD = "### function `foo`  \n\n---\n→ `int`  \nParameters:  \n- `int x`\n\nAdds one.  \n\n---\n```c\nint foo(int x)\n```"
while True:
    n = 0
    while True:
        l = sys.stdin.buffer.readline()
        if not l: sys.exit(0)
        if l.strip() == b'': break
        if l.lower().startswith(b'content-length:'): n = int(l.split(b':')[1])
    m = json.loads(sys.stdin.buffer.read(n))
    me = m.get('method')
    log.write('%s %s\n' % (me, json.dumps(m.get('params', {}).get('position', m.get('params', {}).get('id', ''))))); log.flush()
    if me == 'initialize':
        send({'jsonrpc': '2.0', 'id': m['id'], 'result': {'capabilities': {'hoverProvider': True, 'textDocumentSync': 2}}})
    elif me == 'textDocument/hover':
        L = m['params']['position']['line']
        r = {0: {'contents': {'kind': 'markdown', 'value': 'ANSWER FOR LINE 0\n```c\nint x\n```'}, 'range': {'start': {'line': 0, 'character': 4}, 'end': {'line': 0, 'character': 5}}},
             1: None, 2: {'contents': {'kind': 'markdown', 'value': CLANGD}}}.get(L)
        send({'jsonrpc': '2.0', 'id': m['id'], 'result': r})
    elif me == 'shutdown': send({'jsonrpc': '2.0', 'id': m['id'], 'result': None})
    elif me == 'exit': sys.exit(0)
    elif 'id' in m and me: send({'jsonrpc': '2.0', 'id': m['id'], 'result': None})
