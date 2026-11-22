#!/usr/bin/env python3
"""A scripted stand-in for `lake serve` (review of PR 111).  argv[1]: scenario
(sev, flood, cursor, plain, deep, strgoals); argv[2]: a log file (one line per request)."""
import json, sys
scen, log = sys.argv[1], open(sys.argv[2], "a")
inp, out = sys.stdin.buffer, sys.stdout.buffer
def send(o):
    b = (o if isinstance(o, bytes) else json.dumps(o).encode())
    out.write(b"Content-Length: %d\r\n\r\n" % len(b) + b); out.flush()
def recv():
    n = 0
    while True:
        l = inp.readline()
        if not l: sys.exit(0)
        if l == b"\r\n": break
        if l.lower().startswith(b"content-length:"): n = int(l[15:])
    return json.loads(inp.read(n))
uri, ver = None, 0
def say():
    d = []
    if scen == "sev":
        d = [{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}}, "severity": 7, "message": "m"}]
    send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": uri, "version": ver, "diagnostics": d}})
    send({"jsonrpc": "2.0", "method": "$/lean/fileProgress", "params": {"textDocument": {"uri": uri, "version": ver}, "processing": []}})
while True:
    m = recv()
    meth, i, p = m.get("method"), m.get("id"), m.get("params", {})
    log.write("%s %s\n" % (meth, p.get("method", ""))); log.flush()
    if meth == "initialize": send({"jsonrpc": "2.0", "id": i, "result": {"capabilities": {}}})
    elif meth == "textDocument/didOpen": uri, ver = p["textDocument"]["uri"], p["textDocument"]["version"]; say()
    elif meth == "textDocument/didChange": ver = p["textDocument"]["version"]; say()
    elif meth == "$/lean/rpc/connect":
        if scen == "flood": send({"jsonrpc": "2.0", "id": i, "error": {"code": -32603, "message": "no worker"}})
        else: send({"jsonrpc": "2.0", "id": i, "result": {"sessionId": "7"}})
    elif meth == "$/lean/rpc/call":
        line = p["position"]["line"]
        if p["method"].endswith("TermGoal"): send({"jsonrpc": "2.0", "id": i, "result": None})
        elif scen == "strgoals": send({"jsonrpc": "2.0", "id": i, "result": {"goals": {"a": 1, "b": 2, "c": 3}}})
        elif scen == "deep":
            t = {"text": "x"}
            for _ in range(200): t = {"tag": [{"diffStatus": "wasChanged"}, t]}
            send({"jsonrpc": "2.0", "id": i, "result": {"goals": [{"hyps": [], "type": t}]}})
        else:
            g = {"userName": "c", "hyps": [{"names": ["h"], "type": {"text": "T"}}], "type": {"text": "G"}}
            send({"jsonrpc": "2.0", "id": i, "result": {"goals": [g] * (12 if line == 0 else line - 1)}})
    elif meth == "shutdown": send({"jsonrpc": "2.0", "id": i, "result": None})
    elif meth == "exit": sys.exit(0)
