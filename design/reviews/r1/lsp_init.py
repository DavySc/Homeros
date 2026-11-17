import subprocess, json, sys
p = subprocess.Popen(["lean","--server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
def send(o):
    b=json.dumps(o).encode(); p.stdin.write(b"Content-Length: %d\r\n\r\n"%len(b)+b); p.stdin.flush()
def recv():
    n=0
    while True:
        l=p.stdout.readline().strip()
        if not l: break
        if l.lower().startswith(b"content-length"): n=int(l.split(b":")[1])
    return json.loads(p.stdout.read(n))
send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":None,"rootUri":None,"capabilities":{"general":{"positionEncodings":["utf-8","utf-16"]}}}})
r=recv(); caps=r["result"]["capabilities"]
print("positionEncoding in reply:", caps.get("positionEncoding","<absent: means utf-16>"))
print("serverInfo:", r["result"].get("serverInfo"))
# now prove which unit columns are in: open a doc with a non-BMP char before an error
send({"jsonrpc":"2.0","method":"initialized","params":{}})
text="example : (\"𝔸\" = \"𝔸\") ∧ xyz := sorry\n"
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/A.lean","languageId":"lean4","version":1,"text":text}}})
while True:
    m=recv()
    if m.get("method")=="textDocument/publishDiagnostics" and m["params"]["diagnostics"]:
        for d in m["params"]["diagnostics"]:
            if "xyz" in d["message"] or "nknown" in d["message"]:
                c=d["range"]["start"]["character"]; i=text.index("xyz")
                print("server column of xyz:",c,"| codepoints:",i,"| utf16:",len(text[:i].encode("utf-16-le"))//2,"| utf8 bytes:",len(text[:i].encode()))
                p.kill(); sys.exit()
