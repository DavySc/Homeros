/* Package 22's clients against live servers (src/sql/README.md starts
   them; CI has them as services): cancel, transactions, refusals, then the
   value oracles tests/test_sql_tds.py and tests/test_sql_pg.py, which check
   every type's text against the server's own, through this program's
   driver mode:
     test_sql run [-p] [-t] [-q] [-c ROWS] < batch
   runs the batch on SQL Server (sa on 127.0.0.1:1433, SQL_SA_PASSWORD) or
   with -p on PostgreSQL (postgres on 127.0.0.1:5432, SQL_PG_PASSWORD),
   prints the reply (rows and messages in order) on stdout and the times on
   stderr: login, first row, total (ms), rows.  -t trusts the server's own
   certificate; -q prints no rows; -c ROWS pauses 300 ms after ROWS rows,
   cancels, and reports how long the cancel took. */
#include <string.h>
#include <time.h>
#include "check.h"
#include "../src/sql/pg.h"
#include "../src/sql/tds.h"
#ifdef _WIN32   /* src/sql/check.sh: the same test, built for Windows, under Wine */
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#endif

/* the two clients' states have the same numbers, so one loop drives both */
_Static_assert((int)TDS_READY == (int)PG_READY && (int)TDS_BUSY == (int)PG_BUSY && (int)TDS_DEAD == (int)PG_DEAD, "states");

typedef struct { int pg; Tds t; Pg g; int *state; uint64_t *rows; Str *out; Net *net, *kill; } Db;
static const char *pass;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static int query(Db *d, const char *sql) { return d->pg ? pg_query(&d->g, sql) : tds_batch(&d->t, sql); }
static int cancel(Db *d) { return d->pg ? pg_cancel(&d->g) : tds_cancel(&d->t); }
static void db_close(Db *d) { d->pg ? pg_close(&d->g) : tds_close(&d->t); }

/* one poll loop until state is `until` or there are stop rows (0: no stop) */
static int run(Db *d, int until, uint64_t stop)
{
	while (*d->state != until && *d->state != TDS_DEAD && !(stop && *d->rows >= stop)) {
#ifdef _WIN32   /* the sockets' events: every wake reads (net.h) */
		HANDLE h[2] = {(HANDLE)(INT_PTR)d->net->fd, d->kill && d->kill->fd != -1 ? (HANDLE)(INT_PTR)d->kill->fd : NULL};
		if (WaitForMultipleObjects(h[1] ? 2 : 1, h, FALSE, INFINITE) == WAIT_FAILED) return -1;
		int in = 1;
#else
		int w = d->pg ? pg_pending(&d->g) : tds_pending(&d->t);
		struct pollfd p[2] = {{d->net->fd, (short)(POLLIN | (w ? POLLOUT : 0)), 0}, {d->kill ? d->kill->fd : -1, POLLOUT, 0}};
		if (poll(p, 2, -1) < 0) return -1;
		int in = p[0].revents & (POLLIN | POLLHUP | POLLERR);
#endif
		if ((d->pg ? pg_io(&d->g, in) : tds_io(&d->t, in)) < 0) return -1;
	}
	return *d->state == TDS_DEAD ? -1 : 0;
}

static int login(Db *d, int pg, int trust)
{
	*d = (Db){ .pg = pg, .state = pg ? &d->g.state : &d->t.state, .rows = pg ? &d->g.rows : &d->t.rows,
		.out = pg ? &d->g.out : &d->t.out, .net = pg ? &d->g.net : &d->t.net, .kill = pg ? &d->g.kill : NULL };
	return (pg ? pg_open(&d->g, "127.0.0.1", 5432, "postgres", pass, "postgres", trust)
		: tds_open(&d->t, "127.0.0.1", 1433, "sa", pass, "master", trust)) || run(d, TDS_READY, 0);
}

/* the batch's reply in d->out, cancelled after stop rows if not 0 */
static int batch(Db *d, const char *sql, uint64_t stop)
{
	d->out->n = 0;
	return query(d, sql) || run(d, TDS_READY, stop) || (*d->state == TDS_BUSY && (cancel(d) || run(d, TDS_READY, 0)));
}
static int is(Db *d, const char *sql, const char *want) { return !batch(d, sql, 0) && !strcmp(d->out->s ? d->out->s : "", want); }

static int driver(int argc, char **argv)
{
	int pg = 0, trust = 0, quiet = 0;
	uint64_t stop = 0;
	for (int a = 2; a < argc; a++)
		if (!strcmp(argv[a], "-p")) pg = 1;
		else if (!strcmp(argv[a], "-t")) trust = 1;
		else if (!strcmp(argv[a], "-q")) quiet = 1;
		else if (!strcmp(argv[a], "-c") && a + 1 < argc) stop = strtoull(argv[++a], NULL, 10);
	if (pg) pass = getenv("SQL_PG_PASSWORD");
#ifdef _WIN32   /* the oracles read the bytes the server sent */
	_setmode(0, _O_BINARY), _setmode(1, _O_BINARY);
#endif
	static char sql[1 << 20];
	sql[fread(sql, 1, sizeof sql - 1, stdin)] = 0;
	Db d;
	double t0 = now(), t1, first = 0;
	int r = !pass || login(&d, pg, trust);
	if (!r && !(t1 = now(), r = query(&d, sql)))
		while (!r && *d.state != TDS_READY) {
			d.out->n = 0;
			r = run(&d, TDS_READY, *d.rows + 1);
			if (*d.rows && !first) first = now();
			if (!quiet) fwrite(d.out->s, 1, d.out->n, stdout);
			if (!r && stop && *d.rows >= stop && *d.state == TDS_BUSY) {
				nanosleep(&(struct timespec){0, 300000000}, NULL);
				uint64_t at = *d.rows;
				double c0 = now();
				d.out->n = 0;
				r = cancel(&d) || run(&d, TDS_READY, 0);
				fprintf(stderr, "cancel %.2f rows_after_cancel %llu\n", now() - c0, (unsigned long long)(*d.rows - at));
				if (!quiet) fwrite(d.out->s, 1, d.out->n, stdout);
			}
		}
	if (r) fprintf(stderr, "test_sql: %s\n", pass ? d.net->err : "no password");
	else fprintf(stderr, "login %.2f first_row %.2f total %.2f rows %llu\n", t1 - t0, first ? first - t1 : 0, now() - t1, (unsigned long long)*d.rows);
	if (pass) db_close(&d);
	return r ? 1 : 0;
}

/* a server's password and a login to it, or a clear line why not */
static int up(Db *d, int pg)
{
	const char *env = pg ? "SQL_PG_PASSWORD" : "SQL_SA_PASSWORD";
	if (!(pass = getenv(env))) { fprintf(stderr, "test_sql: %s is not set: start the test servers as src/sql/README.md says\n", env); return 0; }
	if (!login(d, pg, 1)) return 1;
	fprintf(stderr, "test_sql: no %s on 127.0.0.1:%d (%s): start it as src/sql/README.md says\n", pg ? "PostgreSQL" : "SQL Server", pg ? 5432 : 1433, d->net->err);
	db_close(d);
	return 0;
}

/* refusals: the server's reason for a wrong password, 50 times (it comes
   with the server's TLS close_notify, which once hid it in 5 of 40 logins);
   the test server's own certificate unless trusted */
static void refusals(int pg, const char *why)
{
	Db d;
	const char *good = pass;
	pass = "not the password";
	for (int i = 0; i < 50; i++) {
		CHECK(login(&d, pg, 1) && strstr(d.net->err, why));
		db_close(&d);
	}
	pass = good;
	for (int i = 0; i < 2; i++) {   /* twice: Windows's CAs are read once */
		CHECK(login(&d, pg, 0) && strstr(d.net->err, "certificate verify failed"));
		db_close(&d);
	}
}

static void oracle(const char *py, const char *argv0)
{
#ifdef _WIN32   /* src/sql/check.sh runs them with this program under Wine */
	return (void)py, (void)argv0;
#endif
	char cmd[4096];
	snprintf(cmd, sizeof cmd, "python3 %s %s %s", py, argv0, getenv("SEED") ? getenv("SEED") : "1");
	CHECK(!system(cmd));
}

int main(int argc, char **argv)
{
	if (argc > 1) return pass = getenv("SQL_SA_PASSWORD"), driver(argc, argv);
	Db d;
	if (!up(&d, 0)) return 1;
	CHECK(d.t.pkt == 4096);
	/* SSMS's session options */
	CHECK(is(&d, "select @@options o", "o\n5496\n(1 row affected)\n"));
	/* text in both directions: outside the BMP, and Latin-1 */
	CHECK(is(&d, "select N'\U0001F600é' x", "x\n\U0001F600é\n(1 row affected)\n"));
	/* a message's text as it is; a value's escaped, one per cell */
	CHECK(is(&d, "print 'a' + char(9) + 'b' + char(10) + 'c'; select 'a' + char(9) + 'b' + char(10) + 'c' x", "a\tb\nc\nx\na\\tb\\nc\n(1 row affected)\n"));
	/* a cancel while no batch runs sends nothing */
	CHECK(!cancel(&d) && d.t.state == TDS_READY && !tds_pending(&d.t));
	/* a cancel after the batch's last DONE was sent: the server still sends
	   DONE_ATTN, which must end this run, not the next (SqlRun.tla's
	   "firstdone") */
	d.out->n = 0;
	CHECK(!query(&d, "select 1 as one") && !cancel(&d) && !run(&d, TDS_READY, 0));
	CHECK(is(&d, "select 2 as two", "two\n2\n(1 row affected)\n"));
	/* a cancelled result reads on to the server's DONE_ATTN and shows no
	   error of its own; the connection then runs the next batch */
	CHECK(!batch(&d, "select top 2000000 a.object_id from sys.all_objects a, sys.all_objects b", 1000));
	CHECK(d.t.state == TDS_READY && !strstr(d.out->s, "Msg") && d.t.rows >= 1000 && d.t.rows < 2000000);
	CHECK(is(&d, "select 1 as one", "one\n1\n(1 row affected)\n"));
	/* a transaction's descriptor comes from the server and goes back with each batch */
	CHECK(!batch(&d, "begin tran", 0) && d.t.txn);
	CHECK(is(&d, "select @@trancount n", "n\n1\n(1 row affected)\n"));
	CHECK(!batch(&d, "rollback", 0) && !d.t.txn);
	db_close(&d);
	refusals(0, "Login failed for user 'sa'");
	/* nothing listens on port 1: the refusal is reported, not waited on */
	CHECK((tds_open(&d.t, "127.0.0.1", 1, "sa", pass, "master", 1) || run(&d, TDS_READY, 0)) && strstr(d.t.net.err, "refused"));
	tds_close(&d.t);
#ifndef _WIN32   /* the Windows login is Windows's */
	CHECK(tds_open(&d.t, "127.0.0.1", 1433, NULL, NULL, "master", 1) && strstr(d.t.net.err, "needs Windows"));
	tds_close(&d.t);
#endif
	oracle("tests/test_sql_tds.py", argv[0]);

	if (!up(&d, 1)) return 1;
	/* text in both directions; a message's text as it is, a value's escaped */
	CHECK(is(&d, "select '\U0001F600é' x", "x\n\U0001F600é\n(SELECT 1)\n"));
	CHECK(is(&d, "do $$ begin raise notice 'a\tb'; end $$; select E'a\\tb' x", "NOTICE 00000: a\\tb\n(DO)\nx\na\\tb\n(SELECT 1)\n"));
	/* a cancel while no query runs sends nothing */
	CHECK(!cancel(&d) && d.g.state == PG_READY && !pg_pending(&d.g));
	/* a cancelled query shows no error of its own (57014 is ours); the
	   connection then runs the next */
	CHECK(!batch(&d, "select g from generate_series(1, 20000000) g", 1000));
	CHECK(d.g.state == PG_READY && !strstr(d.out->s, "ERROR") && d.g.rows >= 1000 && d.g.rows < 20000000);
	CHECK(is(&d, "select 1 as one", "one\n1\n(SELECT 1)\n"));
	/* the transaction state comes from the server's ReadyForQuery */
	CHECK(!batch(&d, "begin", 0) && d.g.txn == 'T');
	CHECK(!batch(&d, "select 1/0", 0) && d.g.txn == 'E' && strstr(d.out->s, "ERROR 22012: division by zero"));
	CHECK(!batch(&d, "rollback", 0) && d.g.txn == 'I');
	db_close(&d);
	refusals(1, "password authentication failed for user \"postgres\"");
	oracle("tests/test_sql_pg.py", argv[0]);
#ifdef _WIN32   /* winpass.sh's summary line */
	fprintf(stderr, "test_sql: both servers: login, text, cancel, transactions, refusals\n");
#endif
	return 0;
}
