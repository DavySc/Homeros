#!/usr/bin/env python3
"""PR 27 review: a server's answer to a didOpen whose text holds a byte that is
not UTF-8 (what the client sends for a Latin-1 file), then to a valid one.
usage: rawbytes.py FILE server argv..."""
import os, subprocess, sys, time, select, json
path = os.path.abspath(sys.argv[1]); raw = open(path, 'rb').read()
p = subprocess.Popen(sys.argv[2:], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=os.path.dirname(path))
def send(b): p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
def q(b): return b.replace(b'\\', b'\\\\').replace(b'"', b'\\"').replace(b'\n', b'\\n')
send(json.dumps({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': os.getpid(), 'rootUri': None, 'capabilities': {}}}).encode())
time.sleep(1)
send(b'{"jsonrpc":"2.0","method":"initialized","params":{}}')
send(b'{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://' + path.encode() + b'","languageId":"c","version":0,"text":"' + q(raw) + b'"}}}')
end = time.time() + float(os.environ.get('SECS', '6')); out = b''; err = b''
while time.time() < end:
    r = select.select([p.stdout, p.stderr], [], [], 0.2)[0]
    for f in r:
        c = os.read(f.fileno(), 65536)
        if f is p.stdout: out += c
        else: err += c
    if p.poll() is not None: break
print('server exit:', p.poll())
print('publishDiagnostics received:', out.count(b'publishDiagnostics'), 'with', out.count(b'"message"'), 'messages')
for l in err.decode('utf-8', 'replace').splitlines():
    if any(w in l.lower() for w in ('utf', 'error', 'invalid', 'parse', 'e[')): print('stderr:', l[:200])
p.kill()
