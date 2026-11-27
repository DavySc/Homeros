#!/usr/bin/env python3
"""Semantic tokens while typing, on a long Lean file: a request after
every didChange (each cancelling the last) against Neovim's 200 ms
debounce.  Prints, per run, the ms from the last key to the tokens of the
final text and the CPU seconds Lean's processes spent from the first key
until then.  python3 tokens.py RUNS KEYS MS_PER_KEY (lean on PATH)."""
import json, os, select, subprocess, sys, tempfile, time

runs, keys, per = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]) / 1000
here = '/home/claude/pr148/src/lsp/bench'

def cpu(root):
    """utime + stime of root and its descendants, in seconds"""
    kids, tot = {}, 0
    for p in os.listdir('/proc'):
        if p.isdigit():
            try: f = open(f'/proc/{p}/stat').read().rsplit(')', 1)[1].split()
            except OSError: continue
            kids.setdefault(int(f[1]), []).append(int(p)); 
    todo = [root]
    while todo:
        p = todo.pop()
        try: f = open(f'/proc/{p}/stat').read().rsplit(')', 1)[1].split()
        except OSError: continue
        tot += int(f[11]) + int(f[12])
        todo += kids.get(p, [])
    return tot / os.sysconf('SC_CLK_TCK')

def run(debounce):
    d = tempfile.mkdtemp()
    path = os.path.join(d, 'Long.lean')
    with open(path, 'w') as f: subprocess.run([sys.executable, os.path.join(here, 'gen_lean.py')], stdout=f, check=True)
    text = open(path).read()
    p = subprocess.Popen(['lean', '--server'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=d)
    buf, nid = b'', [10]
    def send(o):
        b = json.dumps(o).encode(); p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
    def recv(t):
        nonlocal buf
        end = time.time() + t
        while True:
            if b'\r\n\r\n' in buf:
                h, rest = buf.split(b'\r\n\r\n', 1); n = int(h.split(b':')[1])
                if len(rest) >= n: buf = rest[n:]; return json.loads(rest[:n])
            r, _, _ = select.select([p.stdout], [], [], max(0, end - time.time()))
            if not r: return None
            buf += os.read(p.stdout.fileno(), 1 << 20)
    uri = 'file://' + path
    send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'processId': None, 'rootUri': None, 'capabilities': {}}})
    while (m := recv(60)) and m.get('id') != 0: pass
    send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
    send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'lean4', 'version': 1, 'text': text}}})
    while True:                                   # elaborated once
        m = recv(120)
        if m and m.get('method') == '$/lean/fileProgress' and not m['params']['processing']: break
    line = text.count('\n') // 3                  # a third of the way down: the rest elaborates again
    while not text.split('\n')[line].startswith('theorem'): line += 1
    pending, due, ver, t0, c0 = None, None, 1, time.time(), cpu(p.pid)
    def ask():
        nonlocal pending
        if pending: send({'jsonrpc': '2.0', 'method': '$/cancelRequest', 'params': {'id': pending}})
        nid[0] += 1; pending = nid[0]
        send({'jsonrpc': '2.0', 'id': pending, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
        return ver
    asked_ver, last = None, None
    for k in range(keys + 1):
        if k < keys:
            ver += 1
            col = len(text.split('\n')[line]) + k
            send({'jsonrpc': '2.0', 'method': 'textDocument/didChange', 'params': {'textDocument': {'uri': uri, 'version': ver},
                  'contentChanges': [{'range': {'start': {'line': line, 'character': col}, 'end': {'line': line, 'character': col}}, 'text': ' '}]}})
            last = time.time()
            if debounce: due = last + 0.2
            else: asked_ver = ask()
        end = time.time() + (per if k < keys else 120)
        while time.time() < end:
            if due and time.time() >= due: asked_ver, due = ask(), None
            m = recv(min(0.01, max(0, end - time.time())))
            if not m: continue
            if 'id' in m and 'method' in m: send({'jsonrpc': '2.0', 'id': m['id'], 'result': None})
            if m.get('id') == pending and 'method' not in m:
                pending = None
                if 'result' in m and asked_ver == ver and k == keys:
                    took, c = time.time() - last, cpu(p.pid) - c0
                    dd = m['result']['data']; print('   the answer measured: %d tokens, the last on line %d of %d' % (len(dd)//5, sum(dd[0::5])+1, text.count(chr(10))), flush=True)
                    p.kill(); p.wait()
                    return took * 1000, c
    p.kill(); p.wait()
    return None, None

for r in range(runs):
    for deb in (0, 1):
        ms, c = run(deb)
        print(f"{'debounce 200 ms' if deb else 'every change    '}  run {r + 1}: {ms:.0f} ms after the last key, {c:.2f} s CPU", flush=True)
