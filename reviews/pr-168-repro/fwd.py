import socket, sys, threading
l = socket.socket(); l.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); l.bind(("127.0.0.1", int(sys.argv[1]))); l.listen(8)
def pipe(a, b):
    try:
        while (d := a.recv(65536)): b.sendall(d)
    except OSError: pass
    for x in (a, b):
        try: x.shutdown(socket.SHUT_RDWR)
        except OSError: pass
while True:
    c, _ = l.accept(); u = socket.create_connection(("127.0.0.1", int(sys.argv[2])))
    threading.Thread(target=pipe, args=(c, u), daemon=True).start(); threading.Thread(target=pipe, args=(u, c), daemon=True).start()
