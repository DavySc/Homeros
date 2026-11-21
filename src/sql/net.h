/* One non-blocking connection for the event loop, shared by the SQL
   clients: the socket, TLS on OpenSSL memory buffers (so the socket and
   the loop stay ours), the bytes waiting to be sent. */
#ifndef SQL_NET_H
#define SQL_NET_H
#include <stddef.h>
#include "str.h"

typedef struct ssl_st SSL;
typedef struct {
	int fd, tls;   /* tls: every byte goes through TLS (the handshake is done) */
	SSL *ssl;
	Str snd;       /* bytes for the socket */
	char err[256];
} Net;

/* Starts a non-blocking connect.  The server's certificate is checked
   against the system's CAs and the host name unless trust (sqlcmd -C,
   psql sslmode=require). */
int  net_open(Net *c, const char *host, int port, int trust);
int  net_send(Net *c, const void *s, size_t n);   /* sealed once tls */
/* handshake bytes in, ours to out: 1 done (send out, then set tls), 0 more, -1 failed */
int  net_shake(Net *c, const char *in, size_t n, Str *out);
int  net_flush(Net *c);                           /* -1: the connection is gone */
int  net_read(Net *c, Str *in);                   /* what has arrived, opened; -1: gone */
void net_close(Net *c);

/* Text of a value, one value per cell and one row per line: code point u
   (< 0x110000) or n bytes of UTF-8, with tab, newline, CR and backslash
   escaped as \t \n \r \\. */
int  net_cp(Str *b, unsigned u);
int  net_esc(Str *b, const char *s, size_t n);
#endif
