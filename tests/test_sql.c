/* Package 22's SQL Server client against a live server (src/sql/README.md
   starts one; CI has it as a service): cancel, transactions, refusals, then
   tests/test_sql_tds.py, which checks every type's text against the
   server's own, through this program's driver mode:
     test_sql_tds run [-t] [-q] [-c ROWS] < batch
   runs the batch as sa on 127.0.0.1:1433 (SQL_SA_PASSWORD), prints the
   reply (rows and messages in order) on stdout and the times on stderr:
   login, first row, total (ms), rows.  -t trusts the server's own
   certificate; -q prints no rows; -c ROWS pauses 300 ms after ROWS rows,
   cancels, and reports how long the cancel took. */
#include <poll.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include "../src/sql/tds.h"

static const char *pass;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* one poll loop until state is `until` or there are stop rows (0: no stop) */
static int run(Tds *t, int until, uint64_t stop)
{
	while (t->state != until && t->state != TDS_DEAD && !(stop && t->rows >= stop)) {
		struct pollfd p = {t->net.fd, (short)(POLLIN | (tds_pending(t) ? POLLOUT : 0)), 0};
		if (poll(&p, 1, -1) < 0 || tds_io(t, p.revents & (POLLIN | POLLHUP | POLLERR)) < 0) return -1;
	}
	return t->state == TDS_DEAD ? -1 : 0;
}

static int login(Tds *t, int trust)
{
	return tds_open(t, "127.0.0.1", 1433, "sa", pass, "master", trust) || run(t, TDS_READY, 0);
}

/* the batch's reply in t->out, cancelled after cancel rows if not 0 */
static int batch(Tds *t, const char *sql, uint64_t cancel)
{
	t->out.n = 0;
	return tds_batch(t, sql) || run(t, TDS_READY, cancel) || (t->state == TDS_BUSY && (tds_cancel(t) || run(t, TDS_READY, 0)));
}

static int driver(int argc, char **argv)
{
	int trust = 0, quiet = 0;
	uint64_t cancel = 0;
	for (int a = 2; a < argc; a++)
		if (!strcmp(argv[a], "-t")) trust = 1;
		else if (!strcmp(argv[a], "-q")) quiet = 1;
		else if (!strcmp(argv[a], "-c") && a + 1 < argc) cancel = strtoull(argv[++a], NULL, 10);
	static char sql[1 << 20];
	sql[fread(sql, 1, sizeof sql - 1, stdin)] = 0;
	Tds t;
	double t0 = now(), t1, first = 0;
	int r = login(&t, trust);
	if (!r && !(t1 = now(), r = tds_batch(&t, sql)))
		while (!r && t.state != TDS_READY) {
			t.out.n = 0;
			r = run(&t, TDS_READY, t.rows + 1);
			if (t.rows && !first) first = now();
			if (!quiet) fwrite(t.out.s, 1, t.out.n, stdout);
			if (!r && cancel && t.rows >= cancel && t.state == TDS_BUSY) {
				nanosleep(&(struct timespec){0, 300000000}, NULL);
				uint64_t at = t.rows;
				double c0 = now();
				t.out.n = 0;
				r = tds_cancel(&t) || run(&t, TDS_READY, 0);
				fprintf(stderr, "cancel %.2f rows_after_cancel %llu\n", now() - c0, (unsigned long long)(t.rows - at));
				if (!quiet) fwrite(t.out.s, 1, t.out.n, stdout);
			}
		}
	if (r) fprintf(stderr, "test_sql_tds: %s\n", t.net.err);
	else fprintf(stderr, "login %.2f first_row %.2f total %.2f rows %llu\n", t1 - t0, first ? first - t1 : 0, now() - t1, (unsigned long long)t.rows);
	tds_close(&t);
	return r ? 1 : 0;
}

int main(int argc, char **argv)
{
	pass = getenv("SQL_SA_PASSWORD");
	if (!pass) { fprintf(stderr, "test_sql_tds: SQL_SA_PASSWORD is not set: start the test server as src/sql/README.md says\n"); return 1; }
	if (argc > 1) return driver(argc, argv);
	Tds t;
	if (login(&t, 1)) {
		fprintf(stderr, "test_sql_tds: no SQL Server on 127.0.0.1:1433 (%s): start the container as src/sql/README.md says\n", t.net.err);
		tds_close(&t);
		return 1;
	}
	CHECK(t.pkt == 4096);
	/* SSMS's session options */
	CHECK(!batch(&t, "select @@options o", 0) && !strcmp(t.out.s, "o\n5496\n(1 row affected)\n"));
	/* text in both directions: outside the BMP, and Latin-1 */
	CHECK(!batch(&t, "select N'\U0001F600\u00E9' x", 0) && !strcmp(t.out.s, "x\n\U0001F600\u00E9\n(1 row affected)\n"));
	/* a message's text as it is; a value's escaped, one per cell */
	CHECK(!batch(&t, "print 'a' + char(9) + 'b' + char(10) + 'c'; select 'a' + char(9) + 'b' + char(10) + 'c' x", 0));
	CHECK(!strcmp(t.out.s, "a\tb\nc\nx\na\\tb\\nc\n(1 row affected)\n"));
	/* a cancel while no batch runs sends nothing */
	CHECK(!tds_cancel(&t) && t.state == TDS_READY && !tds_pending(&t));
	/* a cancel after the batch's last DONE was sent: the server still sends
	   DONE_ATTN, which must end this run, not the next (SqlRun.tla's
	   "firstdone") */
	t.out.n = 0;
	CHECK(!tds_batch(&t, "select 1 as one") && !tds_cancel(&t) && !run(&t, TDS_READY, 0));
	CHECK(!batch(&t, "select 2 as two", 0) && !strcmp(t.out.s, "two\n2\n(1 row affected)\n"));
	/* a cancelled result reads on to the server's DONE_ATTN and shows no
	   error of its own; the connection then runs the next batch */
	CHECK(!batch(&t, "select top 2000000 a.object_id from sys.all_objects a, sys.all_objects b", 1000));
	CHECK(t.state == TDS_READY && !strstr(t.out.s, "Msg") && t.rows >= 1000 && t.rows < 2000000);
	CHECK(!batch(&t, "select 1 as one", 0) && !strcmp(t.out.s, "one\n1\n(1 row affected)\n"));
	/* a transaction's descriptor comes from the server and goes back with each batch */
	CHECK(!batch(&t, "begin tran", 0) && t.txn);
	CHECK(!batch(&t, "select @@trancount n", 0) && !strcmp(t.out.s, "n\n1\n(1 row affected)\n"));
	CHECK(!batch(&t, "rollback", 0) && !t.txn);
	tds_close(&t);
	/* the server's reason for a refused login */
	const char *good = pass;
	pass = "not the password";
	CHECK(login(&t, 1) && strstr(t.net.err, "Login failed for user 'sa'"));
	tds_close(&t);
	pass = good;
	/* the test server's own certificate is refused unless trusted */
	CHECK(login(&t, 0) && strstr(t.net.err, "certificate verify failed"));
	tds_close(&t);
	char cmd[4096];
	snprintf(cmd, sizeof cmd, "python3 tests/test_sql_tds.py %s %s", argv[0], getenv("SEED") ? getenv("SEED") : "1");
	CHECK(!system(cmd));
	return 0;
}
