/* One connection to SQL Server (tds.h) or PostgreSQL (pg.h) behind one
   interface, so the editor and the tests drive both through one loop.
   The two clients' states have the same numbers (DB_* below). */
#ifndef SQL_DB_H
#define SQL_DB_H
#include "pg.h"
#include "tds.h"

enum { DB_READY = TDS_READY, DB_BUSY = TDS_BUSY, DB_DEAD = TDS_DEAD };
_Static_assert((int)TDS_READY == (int)PG_READY && (int)TDS_BUSY == (int)PG_BUSY
	&& (int)TDS_CANCEL == (int)PG_CANCEL && (int)TDS_DEAD == (int)PG_DEAD, "the clients' states");

typedef struct { int pg; union { Tds t; Pg g; }; } Db;

int  db_open(Db *d, int pg, const char *host, int port, const char *user, const char *pass, const char *db, int trust);
int  db_query(Db *d, const char *sql);
int  db_cancel(Db *d);
int  db_pending(const Db *d);
int  db_io(Db *d, int readable);
void db_close(Db *d);
static inline int db_state(const Db *d) { return d->pg ? d->g.state : d->t.state; }
static inline uint64_t db_rows(const Db *d) { return d->pg ? d->g.rows : d->t.rows; }
static inline Str *db_out(Db *d) { return d->pg ? &d->g.out : &d->t.out; }
static inline Net *db_net(Db *d) { return d->pg ? &d->g.net : &d->t.net; }
static inline int db_kill_fd(const Db *d) { return d->pg ? d->g.kill.fd : -1; }   /* Postgres's cancel connection */
#endif
