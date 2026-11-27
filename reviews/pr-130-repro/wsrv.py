import socket, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(5)
while True:
    c, _ = s.accept()
    time.sleep(1)
    c.settimeout(8)
    try:
        while c.recv(1 << 20): pass
    except Exception: pass
    c.close()
