/* A minimal TDS 7.4 client for one event loop: no thread, no blocking
   call.  The caller polls t->net.fd (for writing too while tds_pending)
   and calls tds_io; everything the server says, rows and messages, comes
   out as text in t->out in the order the server sent it (dbbliss dropped
   what follows an error: KB chapter 11).  Always TLS; SQL logins. */
#ifndef SQL_TDS_H
#define SQL_TDS_H
#include <stdint.h>
#include "net.h"

typedef struct {
	uint8_t type, prec, scale;
	uint32_t len;   /* the declared length; 0xFFFF on a (max) type */
	int plp;        /* sent in chunks (varchar(max), xml, udt) */
} TdsCol;

typedef struct {
	Net net;             /* the socket, TLS, err */
	int state;           /* TDS_* below */
	Str raw, msg, out, login, plp;
	size_t at, tried;    /* parsed up to msg.s+at; bytes there when a token was last short */
	int eom;             /* the last packet of a reply has arrived */
	uint32_t pkt;        /* packet size the server agreed to */
	uint64_t txn;        /* the open transaction's descriptor, 0 for none (from the server, never guessed) */
	TdsCol *col; size_t ncol;
	uint64_t rows, sets; /* rows rendered, result sets */
	int quiet;           /* parse rows but render nothing (while cancelling) */
} Tds;

enum { TDS_PRELOGIN, TDS_TLS, TDS_LOGIN, TDS_READY, TDS_BUSY, TDS_CANCEL, TDS_DEAD };

/* Starts a non-blocking connect; the login follows the server's prelogin reply. */
int  tds_open(Tds *t, const char *host, int port, const char *user, const char *pass, const char *db, int trust);
int  tds_batch(Tds *t, const char *sql);   /* -1 unless TDS_READY */
int  tds_cancel(Tds *t);                   /* an attention; the reply ends at the server's DONE_ATTN */
int  tds_pending(const Tds *t);            /* bytes waiting to be sent */
int  tds_io(Tds *t, int readable);         /* -1: the connection is gone (t->net.err) */
/* Parses the reply bytes in t->msg (packet payloads, t->eom once the last
   one is in) as far as whole tokens go: tds_io's, and the fuzzer's. */
int  tds_parse(Tds *t);
void tds_close(Tds *t);
#endif
