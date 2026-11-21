/* A minimal PostgreSQL v3 client for one event loop, the twin of tds.h:
   the caller polls p->net.fd (POLLOUT while pg_pending) and calls pg_io;
   rows (the server's own text format) and messages come out as text in
   p->out in server order.  Always TLS (SSLRequest), SCRAM-SHA-256. */
#ifndef PG_H
#define PG_H
#include "net.h"

typedef struct {
	Net net, kill;       /* the connection; a second one only to cancel */
	int state;           /* PG_* below */
	Buf in, out, start;  /* bytes from the server (opened); text for the caller; startup message */
	size_t at;           /* parsed up to in.p+at */
	char host[256], pass[128], nonce[32], first[256];   /* where to send a cancel; SCRAM: the password and our first message */
	int port;
	uint8_t salted[32];
	uint32_t pid, key;   /* BackendKeyData, for a cancel */
	char txn;            /* ReadyForQuery: I idle, T in a transaction, E failed transaction */
	uint64_t rows, sets; /* rows rendered, result sets */
	int quiet;           /* parse rows but render nothing (while cancelling) */
} Pg;

enum { PG_SSL, PG_TLS, PG_AUTH, PG_READY, PG_BUSY, PG_CANCEL, PG_DEAD };

int pg_open(Pg *p, const char *host, int port, const char *user, const char *pass, const char *db, int trust);
int pg_query(Pg *p, const char *sql);
int pg_cancel(Pg *p);
int pg_pending(const Pg *p);
int pg_io(Pg *p, int readable);   /* -1: the connection is gone (p->net.err) */
void pg_close(Pg *p);
#endif
