# test_sql_wake.c's second part with a wake from another cause while the
# socket is full (the server's byte), so the caller's flush blocks and the
# bytes are still queued when the read resets the event.  Usage: wake-test.py FILE
import sys
p = sys.argv[1]
s = open(p).read()
old = "\tCHECK(c.snd.n);   /* a send would block */\n\tsize_t got = 0;\n"
new = ("\tCHECK(c.snd.n);   /* a send would block */\n"
       "\t/* a wake from another cause while the socket is full (the server's\n"
       "\t   byte): the caller's flush blocks, the socket turns writable, the\n"
       "\t   read resets the event and must send */\n"
       "\tCHECK(send(s, b, 1, 0) == 1 && wait_ms(&c, 3000));\n"
       "\tCHECK(!net_flush(&c) && c.snd.n);\n"
       "\tsize_t got = writable(&c, s);\n"
       "\tCHECK(!net_read(&c, &in));\n")
assert s.count(old) == 1
s = s.replace(old, new)
end = "\tnet_close(&c);\n\tclosesocket(s);\n"
assert s.count(end) == 1
open(p, 'w').write(s.replace(end, end + "\tstr_free(&in);   /* the server's byte */\n"))
