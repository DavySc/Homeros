# PR 101 review: does the real Lean server (tools-env's lean --server) take a
# textDocument/didSave without text, as lsp_saved sends it, and what does it
# advertise for textDocumentSync.save?   python3 didsave.py
import json, os, subprocess, tempfile, time, select
d = tempfile.mkdtemp()
path = os.path.join(d, "A.lean")
open(path, "w").write("theorem t : 1 + 1 = 2 := rfl\n")
uri = "file://" + path
p = subprocess.Popen(["lean", "--server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=d)
def send(m):
    b = json.dumps(m).encode()
    p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(b) + b); p.stdin.flush()
buf = b""
def recv(timeout):
    global buf
    end = time.time() + timeout
    while time.time() < end:
        i = buf.find(b"\r\n\r\n")
        if i >= 0:
            n = int(buf[:i].split(b"Content-Length: ")[1].split(b"\r\n")[0])
            if len(buf) >= i + 4 + n:
                m = json.loads(buf[i + 4:i + 4 + n]); buf = buf[i + 4 + n:]; return m
        if select.select([p.stdout], [], [], 0.2)[0]:
            c = os.read(p.stdout.fileno(), 65536)
            if not c: return None
            buf += c
    return None
def drain(t, label):
    seen = []
    while True:
        m = recv(t)
        if m is None: break
        seen.append(m.get("method") or ("reply %s" % m.get("id")))
        if m.get("method") == "textDocument/publishDiagnostics" and m["params"]["diagnostics"]:
            print(label, "diagnostics:", [x["message"][:80] for x in m["params"]["diagnostics"]])
        if "error" in m: print(label, "ERROR", m["error"])
    print(label, sorted(set(seen)))
send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"processId": os.getpid(), "rootUri": "file://" + d,
      "capabilities": {"textDocument": {"synchronization": {"didSave": True}}}}})
while True:
    m = recv(60)
    if m is None or m.get("id") == 1: break
print("textDocumentSync:", json.dumps(m["result"]["capabilities"].get("textDocumentSync")))
send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "languageId": "lean4", "version": 1, "text": open(path).read()}}})
drain(15, "after didOpen:")
for i in range(3):
    send({"jsonrpc": "2.0", "method": "textDocument/didSave", "params": {"textDocument": {"uri": uri}}})
drain(6, "after 3 didSave:")
send({"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 8}}})
while True:
    m = recv(15)
    if m is None or m.get("id") == 2: break
print("hover after didSave:", "answered" if m and "result" in m else m)
print("server alive:", p.poll() is None)
p.kill()
