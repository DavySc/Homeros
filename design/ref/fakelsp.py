#!/usr/bin/env python3
"""Minimal LSP server that logs every message (with ms timestamps) to $FAKELSP_LOG.
Answers initialize, hover (after a delay, so cancellation is observable), shutdown."""
import json, os, sys, time, threading
log = open(os.environ.get("FAKELSP_LOG", "/tmp/fakelsp.log"), "a", buffering=1)
t0 = time.time(); lock = threading.Lock()
def send(o):
    b = json.dumps(o).encode()
    with lock:
        sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(b) + b); sys.stdout.buffer.flush()
def rec(m):
    p = m.get("params", {})
    short = {k: p[k] for k in ("contentChanges", "textDocument", "position", "id") if k in p}
    log.write(json.dumps({"ms": int((time.time() - t0) * 1000), "method": m.get("method"), "id": m.get("id"), **short}) + "\n")
def reply_later(i, res, d):
    time.sleep(d); send({"jsonrpc": "2.0", "id": i, "result": res})
while True:
    h = {}
    while True:
        l = sys.stdin.buffer.readline()
        if not l: sys.exit(0)
        l = l.strip()
        if not l: break
        k, v = l.split(b":", 1); h[k.lower()] = v.strip()
    m = json.loads(sys.stdin.buffer.read(int(h[b"content-length"])))
    rec(m)
    meth = m.get("method")
    if meth == "initialize":
        send({"jsonrpc": "2.0", "id": m["id"], "result": {"capabilities": {
            "textDocumentSync": {"openClose": True, "change": 2, "save": {"includeText": False}},
            "hoverProvider": True}}})
    elif meth == "textDocument/hover":
        threading.Thread(target=reply_later, args=(m["id"], {"contents": "hover"}, 2.0), daemon=True).start()
    elif meth == "shutdown":
        send({"jsonrpc": "2.0", "id": m["id"], "result": None})
    elif meth == "exit":
        sys.exit(0)
    elif "id" in m and meth:
        send({"jsonrpc": "2.0", "id": m["id"], "result": None})
