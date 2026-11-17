#!/usr/bin/env python3
"""PR 27 review: what Lean itself publishes for a file (raw ranges).
usage: leanraw.py FILE.lean [seconds]"""
import json, os, subprocess, sys, time, select
path = os.path.abspath(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20
text = open(path, encoding='utf-8').read()
p = subprocess.Popen(['lean', '--server'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=os.path.dirname(path))
def send(o):
    b = json.dumps(o).encode(); p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
buf = b''
def recv(deadline):
    global buf
    while True:
        i = buf.find(b'\r\n\r\n')
        if i >= 0:
            n = int([l for l in buf[:i].split(b'\r\n') if l.lower().startswith(b'content-length')][0].split(b':')[1])
            if len(buf) >= i + 4 + n:
                m = buf[i+4:i+4+n]; buf = buf[i+4+n:]; return json.loads(m)
        t = deadline - time.time()
        if t <= 0 or not select.select([p.stdout], [], [], t)[0]: return None
        c = os.read(p.stdout.fileno(), 65536)
        if not c: return None
        buf += c
uri = 'file://' + path
send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': os.getpid(), 'rootUri': 'file://' + os.path.dirname(path), 'capabilities': {'textDocument': {'publishDiagnostics': {'versionSupport': True}}, 'lean': {'incrementalDiagnosticSupport': True}}}})
end = time.time() + secs
while True:
    m = recv(end)
    if m is None: break
    if m.get('id') == 0:
        send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
        send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'lean4', 'version': 0, 'text': text}}})
    elif m.get('method') == 'textDocument/publishDiagnostics':
        print('publish v%s incr=%s n=%d' % (m['params'].get('version'), m['params'].get('isIncremental'), len(m['params']['diagnostics'])))
        for d in m['params']['diagnostics']:
            r = d['range']; print('diag v%s incr=%s %d:%d-%d:%d full=%s %r' % (m['params'].get('version'), m['params'].get('isIncremental'), r['start']['line'], r['start']['character'], r['end']['line'], r['end']['character'], d.get('fullRange'), d['message'][:70]))
    elif m.get('method') == '$/lean/fileProgress':
        pr = m['params']['processing']
        print('progress v%s %s' % (m['params']['textDocument'].get('version'), [(x['range']['start']['line'], x['range']['end']['line']) for x in pr]))
        if not pr: end = min(end, time.time() + 1.0)
p.kill()
