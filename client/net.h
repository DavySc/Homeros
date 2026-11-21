/* One non-blocking connection for the editor's event loop, shared by the
   SQL Server and PostgreSQL clients: the socket, TLS on OpenSSL memory
   buffers (so the socket and the loop stay ours), the bytes to send. */
#ifndef NET_H
#define NET_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *p; size_t n, cap; } Buf;
int buf_put(Buf *b, const void *s, size_t n);
int buf_c(Buf *b, int c);
int buf_cp(Buf *b, uint32_t u);   /* UTF-8, \t \n \r \\ escaped (one value per cell) */
int buf_esc(Buf *b, const uint8_t *s, size_t n);   /* bytes as they are, the same four escaped */

typedef struct ssl_st SSL;
typedef struct {
	int fd, tls;   /* tls: every byte through TLS (the handshake is done) */
	SSL *ssl;
	Buf snd;       /* bytes for the socket */
	char err[256];
} Net;

/* connects; the certificate is checked against the system's CAs and the
   host name unless trust (sqlcmd -C, psql sslmode=require) */
int net_open(Net *c, const char *host, int port, int trust);
int net_send(Net *c, const void *s, size_t n);                  /* sealed once tls */
int net_shake(Net *c, const uint8_t *in, size_t n, Buf *out);   /* handshake bytes in, ours to out: 1 done (send out, then set tls), 0 more, -1 failed */
int net_flush(Net *c);                                          /* -1: the connection is gone */
int net_read(Net *c, Buf *in);                                  /* what has arrived, opened; -1: gone */
void net_close(Net *c);
#endif
