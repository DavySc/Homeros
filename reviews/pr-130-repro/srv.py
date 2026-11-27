import socket, sys
n = int(sys.argv[2]); rst = len(sys.argv) > 3
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(5)
while True:
    c, _ = s.accept()
    c.sendall(b"x" * n)
    c.close()
