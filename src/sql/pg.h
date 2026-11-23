/* A minimal PostgreSQL client for one event loop, the twin of tds.h: the
   caller polls p->net.fd (for writing too while pg_pending) and, while a
   cancel's connection is open (p->kill.fd >= 0), p->kill.fd for reading
   too: the cancel ends when the server closes it, which may be the last
   thing to come; then calls pg_io; rows (in the server's own text format) and messages come out as
   text in p->out in the order the server sent them.  Always TLS
   (SSLRequest), SCRAM-SHA-256. */
#ifndef SQL_PG_H
#define SQL_PG_H
#include <stdint.h>
#include "net.h"

typedef struct {
	Net net, kill;       /* the connection; a second one only to cancel */
	int state;           /* PG_* below */
	Str in, out, start;  /* bytes from the server (opened); text for the caller; the startup message */
	size_t at;           /* parsed up to in.s+at */
	char host[256], pass[128], nonce[32], first[256];   /* where a cancel goes; SCRAM: the password, our first message */
	int port;
	uint8_t salted[32];
	uint32_t pid, key;   /* BackendKeyData, for a cancel */
	char txn;            /* ReadyForQuery: I idle, T in a transaction, E a failed transaction */
	uint64_t rows, sets; /* rows rendered, result sets */
	int quiet;           /* parse rows but render nothing (while cancelling) */
	int ready;           /* ReadyForQuery came while the cancel's connection was still open */
} Pg;

enum { PG_SSL, PG_TLS, PG_AUTH, PG_READY, PG_BUSY, PG_CANCEL, PG_DEAD };

int  pg_open(Pg *p, const char *host, int port, const char *user, const char *pass, const char *db, int trust);
int  pg_query(Pg *p, const char *sql);
int  pg_cancel(Pg *p);   /* only while a query runs; 0 otherwise */
int  pg_pending(const Pg *p);
int  pg_io(Pg *p, int readable);   /* -1: the connection is gone (p->net.err) */
int  pg_parse(Pg *p);    /* every whole message in p->in (pg_io's; the fuzzer's) */
void pg_close(Pg *p);
#endif
