#include "db.h"

int db_open(Db *d, int pg, const char *host, int port, const char *user, const char *pass, const char *db, int trust)
{
	d->pg = pg;
	return pg ? pg_open(&d->g, host, port, user, pass, db, trust) : tds_open(&d->t, host, port, user, pass, db, trust);
}
int db_query(Db *d, const char *sql) { return d->pg ? pg_query(&d->g, sql) : tds_batch(&d->t, sql); }
int db_cancel(Db *d) { return d->pg ? pg_cancel(&d->g) : tds_cancel(&d->t); }
int db_pending(const Db *d) { return d->pg ? pg_pending(&d->g) : tds_pending(&d->t); }
int db_io(Db *d, int readable) { return d->pg ? pg_io(&d->g, readable) : tds_io(&d->t, readable); }
void db_close(Db *d) { if (d->pg) pg_close(&d->g); else tds_close(&d->t); }
