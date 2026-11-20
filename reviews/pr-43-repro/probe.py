#!/usr/bin/env python3
"""Probes of the child alone: python3 probe.py CHILD ROOT [test...]
CHILD is a homeros-infoview binary, ROOT a directory with the page's files
(install.sh's).  Each probe starts a fresh child with this script as its
editor and prints what it measured."""
import os, socket, subprocess, sys, tempfile, time, re, resource

CHILD, ROOT = sys.argv[1], sys.argv[2]
want = sys.argv[3:]

class Child:
    def __init__(self, env=None):
        self.dir = tempfile.mkdtemp(prefix="r43-")
        op = os.path.join(self.dir, "open.sh")
        open(op, "w").write("#!/bin/sh\necho \"$1\" >> %s/opened\n" % self.dir)
        os.chmod(op, 0o755)
        e = dict(os.environ, XDG_RUNTIME_DIR=self.dir)
        e.update(env or {})
        self.p = subprocess.Popen([CHILD, ROOT, op], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=e)
        self.redirect = self.p.stdout.readline().decode().strip()
        if not self.redirect: return
        html = open(self.redirect).read()
        m = re.search(r"url=http://127\.0\.0\.1:(\d+)/([0-9a-f]+)/", html)
        self.port, self.token = int(m.group(1)), m.group(2)
        os.set_blocking(self.p.stdout.fileno(), False)
    def lines(self, wait=0.3):
        time.sleep(wait)
        try: return (self.p.stdout.read() or b"").decode(errors="replace").splitlines()
        except BlockingIOError: return []
    def conn(self, host="127.0.0.1"):
        s = socket.create_connection((host, self.port), timeout=3)
        return s
    def req(self, raw, read=True, timeout=3):
        s = self.conn()
        s.sendall(raw)
        if not read: return s
        s.settimeout(timeout)
        b = b""
        try:
            while True:
                d = s.recv(65536)
                if not d: break
                b += d
                h, _, body = b.partition(b"\r\n\r\n")
                m = re.search(rb"Content-Length: (\d+)", h)
                if _ and m and len(body) >= int(m.group(1)): break
        except socket.timeout: pass
        except ConnectionError: b = b""
        s.close()
        return b
    def status(self, raw):
        r = self.req(raw)
        return r.split(b"\r\n")[0].decode(errors="replace") if r else "(no answer)"
    def rss(self):
        for l in open("/proc/%d/status" % self.p.pid):
            if l.startswith("VmRSS"): return int(l.split()[1]) // 1024
    def end(self):
        self.p.stdin.close()
        try: return self.p.wait(3)
        except subprocess.TimeoutExpired: self.p.kill(); return "killed"

def t_bind():
    c = Child()
    # /proc/net/tcp: local address (hex, little-endian) of the listening socket
    for l in open("/proc/net/tcp").read().splitlines()[1:]:
        f = l.split()
        if f[3] == "0A" and int(f[1].split(":")[1], 16) == c.port:
            a = bytes.fromhex(f[1].split(":")[0])[::-1]
            print("  listens on %s:%d" % (".".join(map(str, a)), c.port))
    ips = subprocess.run(["hostname", "-I"], capture_output=True, text=True).stdout.split()
    for ip in ips + ["::1"]:
        try: c.conn(ip).close(); print("  connect to", ip, "ACCEPTED")
        except OSError as e: print("  connect to", ip, "refused:", e.strerror or e)
    c.end()

def t_token():
    c = Child(); T = c.token
    for name, raw in [
        ("page, right token", "GET /%s/ HTTP/1.1\r\n\r\n" % T),
        ("no token", "GET / HTTP/1.1\r\n\r\n"),
        ("no token, events", "GET /events?page=a HTTP/1.1\r\n\r\n"),
        ("no token, call", "POST /call?page=a HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}"),
        ("token as a query", "GET /?%s/ HTTP/1.1\r\n\r\n" % T),
        ("token without the slash", "GET /%s HTTP/1.1\r\n\r\n" % T),
        ("token in upper case", "GET /%s/ HTTP/1.1\r\n\r\n" % T.upper()),
        ("token, first 63", "GET /%s/ HTTP/1.1\r\n\r\n" % T[:63]),
        ("token + 1 character", "GET /%sa/ HTTP/1.1\r\n\r\n" % T),
        ("absolute-form URI", "GET http://127.0.0.1:%d/%s/ HTTP/1.1\r\n\r\n" % (c.port, T)),
    ]:
        print("  %-26s %s" % (name, c.status(raw.encode())))
    print("lines to the editor:", c.lines())
    c.end()

def t_traverse():
    c = Child(); T = c.token
    open(os.path.join(c.dir, "secret"), "w").write("secret")
    names = ["../secret", "dist/../../secret", "dist/..", "..", ".", "./index.html", "dist/./index.css", "dist//index.css",
             "%2e%2e/secret", "dist/%2e%2e/%2e%2e/etc/passwd", "..%2fsecret", "dist\\..\\..\\secret", "/etc/passwd",
             "//etc/passwd", "~/.bashrc", "dist/", "dist", "index.html%00.js", "index.html\x00", "index.html/",
             "dist/...", "dist/..index.css", "a" * 5000, "dist/" + "a/" * 2000 + "x", "index.html?x=../../etc/passwd",
             "index.html#../", "dist/index.css/../../../etc/passwd", "INDEX.HTML", ".git/config", "dist/.x"]
    bad = 0
    for n in names:
        s = c.status(("GET /%s/%s HTTP/1.1\r\n\r\n" % (T, n)).encode("latin1"))
        ok = "404" in s or (n.startswith("index.html?") and "200" in s) or "(no answer)" in s
        if not ok: bad += 1; print("  SERVED?", repr(n[:60]), s)
    print("traversal: %d names, %d served that should not be" % (len(names), bad))
    # a symlink inside ROOT is followed (ROOT is the user's own)
    c.end()

def t_origin():
    c = Child(); T = c.token
    hdr = "Host: evil.example:%d\r\nOrigin: http://evil.example\r\nReferer: http://evil.example/x\r\nSec-Fetch-Site: cross-site\r\n" % c.port
    print("  page with Host/Origin evil.example:", c.status(("GET /%s/ HTTP/1.1\r\n%s\r\n" % (T, hdr)).encode()))
    body = '{"id":0,"m":"applyEdit","a":[{}]}'
    print("  call with Host/Origin evil.example:", c.status(("POST /%s/call?page=x HTTP/1.1\r\n%sContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s" % (T, hdr, len(body), body)).encode()))
    print("  lines to the editor:", c.lines())
    r = c.req(("GET /%s/ HTTP/1.1\r\n\r\n" % T).encode())
    print("  response headers of the page:", r.split(b"\r\n\r\n")[0].decode().split("\r\n")[1:])
    c.end()

def t_memory(nconn=16, mb=60):
    c = Child()
    print("  RSS before: %d MB" % c.rss())
    chunk = b"x" * (1 << 20)
    ss = []
    refused = False
    for i in range(nconn):
        s = c.conn()
        try:
            s.sendall(b"POST /nothing HTTP/1.1\r\nContent-Length: %d\r\n\r\n" % (64 << 20))
            for _ in range(mb): s.sendall(chunk)
        except ConnectionError: refused = True
        ss.append(s)
    time.sleep(0.5)
    print("  RSS with %d connections, no token, %d MB sent on each: %d MB" % (nconn, mb, c.rss()))
    if refused: print("  (the child closed the connections while they sent)")
    for s in ss: s.close()
    time.sleep(0.5)
    print("  RSS after they closed: %d MB" % c.rss())
    c.end()

def t_idle():
    c = Child(); T = c.token
    ss = [c.conn() for _ in range(64)]
    time.sleep(0.2)
    t = time.time()
    r = c.status(("GET /%s/ HTTP/1.1\r\n\r\n" % T).encode())
    print("  with 64 idle connections (no token, nothing sent), the page: %s after %.1f s" % (r, time.time() - t))
    time.sleep(20)
    r = c.status(("GET /%s/ HTTP/1.1\r\n\r\n" % T).encode())
    print("  20 s later: %s" % r)
    for s in ss: s.close()
    time.sleep(0.2)
    print("  after they closed: %s" % c.status(("GET /%s/ HTTP/1.1\r\n\r\n" % T).encode()))
    c.end()

def t_rundir():
    for n in (39, 40, 41):
        base = tempfile.mkdtemp(prefix="r")
        d = base + "/" + "d" * (n - len(base) - 1)
        os.mkdir(d)
        c = Child({"XDG_RUNTIME_DIR": d})
        print("  XDG_RUNTIME_DIR of %d characters: first line %r, exit %s" % (len(d), c.redirect, c.p.poll() if not c.redirect else "running"))
        if c.redirect: c.end()

def t_dash():
    c = Child(); T = c.token
    for pid in ("-", "abc"):
        body = '{"id":0,"m":"saveConfig","a":[null]}'
        c.status(("POST /%s/call?page=%s HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s" % (T, pid, len(body), body)).encode())
    print("  lines to the editor:", c.lines())
    c.end()

def t_lost():
    """An answer written to a stream that then breaks unread is not sent again
    (Infoview.tla's Break keeps what the page 'got')."""
    c = Child(); T = c.token
    s = c.req(("GET /%s/events?page=p HTTP/1.1\r\n\r\n" % T).encode(), read=False)
    time.sleep(0.2)
    c.p.stdin.write(b'p {"id":0,"k":"val","v":null}\n'); c.p.stdin.flush()
    time.sleep(0.2)
    s.close()                      # the browser drops the connection, the answer unread
    time.sleep(0.2)
    r = c.req(("GET /%s/events?page=p HTTP/1.1\r\n\r\n" % T).encode(), timeout=1)
    print("  answer on the reopened stream:", b'"id":0' in r, "| lines to the editor:", c.lines())
    c.end()

def t_exit():
    c = Child()
    print("  redirect file mode: %o" % (os.stat(c.redirect).st_mode & 0o777))
    rc = c.end()
    print("  editor closes the pipe: exit %s, file left: %s" % (rc, os.path.exists(c.redirect)))
    c = Child(); c.p.terminate(); c.p.wait()
    print("  SIGTERM: file left: %s" % os.path.exists(c.redirect))

tests = dict(bind=t_bind, token=t_token, traverse=t_traverse, origin=t_origin, memory=t_memory, idle=t_idle,
             rundir=t_rundir, dash=t_dash, lost=t_lost, exit=t_exit)
for name in want or tests:
    print(name + ":")
    tests[name]()
