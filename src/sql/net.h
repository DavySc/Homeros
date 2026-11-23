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
	char name[256];   /* the host's DNS name (FreeTDS's tds_get_spn), for net_sspi */
#ifdef _WIN32
	/* fd is the socket's WSAEventSelect event, which the loop waits on (loop.h) */
	unsigned long long sock;
	int up;   /* the connect is done */
	struct Sspi *sspi;
#endif
} Net;

/* Starts a non-blocking connect.  The server's certificate is checked
   against the system's CAs and the host name unless trust (sqlcmd -C,
   psql sslmode=require). */
int  net_open(Net *c, const char *host, int port, int trust);
/* Sealed once tls.  The caller flushes once the message is queued, as
   libuv's uv_write tries at once: Winsock signals a connected socket
   writable only after a send would block. */
int  net_send(Net *c, const void *s, size_t n);
/* handshake bytes in, ours to out: 1 done (send out, then set tls), 0 more, -1 failed */
int  net_shake(Net *c, const char *in, size_t n, Str *out);
int  net_flush(Net *c);                           /* -1: the connection is gone */
/* What has arrived, opened; -1: gone.  On Windows every wake of fd reads
   (the read resets the event), whatever woke it. */
int  net_read(Net *c, Str *in);
void net_close(Net *c);
/* The Windows login (SSPI Negotiate, as SSMS's): the token for the
   server's token in (n 0: the first, for service principal spn, which the
   later calls keep) into out; 1 done, 0 more, -1 failed (and off Windows). */
int  net_sspi(Net *c, const char *spn, const char *in, size_t n, Str *out);

/* Text of a value, one value per cell and one row per line: code point u
   (< 0x110000) or n bytes of UTF-8, with tab, newline, CR and backslash
   escaped as \t \n \r \\ (net_cp: unless raw, a message's text). */
int  net_cp(Str *b, unsigned u, int raw);
int  net_esc(Str *b, const char *s, size_t n);
#endif
