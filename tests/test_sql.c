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
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "../src/sql/db.h"

static const char *pass;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* one poll loop until state is `until` or there are stop rows (0: no stop) */
static int run(Db *d, int until, uint64_t stop)
{
	while (db_state(d) != until && db_state(d) != DB_DEAD && !(stop && db_rows(d) >= stop)) {
		Net *k = db_kill(d);
		struct pollfd p[2] = {{db_net(d)->fd, (short)(POLLIN | (db_pending(d) ? POLLOUT : 0)), 0}, {k ? k->fd : -1, (short)(POLLIN | (k && k->snd.n ? POLLOUT : 0)), 0}};
		if (poll(p, 2, -1) < 0 || db_io(d, p[0].revents & (POLLIN | POLLHUP | POLLERR)) < 0) return -1;
	}
	return db_state(d) == DB_DEAD ? -1 : 0;
}

static int login(Db *d, int pg, int trust)
{
	return db_open(d, pg, "127.0.0.1", pg ? 5432 : 1433, pg ? "postgres" : "sa", pass, pg ? "postgres" : "master", trust) || run(d, DB_READY, 0);
}

/* the batch's reply in db_out(d), cancelled after stop rows if not 0 */
static int batch(Db *d, const char *sql, uint64_t stop)
{
	db_out(d)->n = 0;
	return db_query(d, sql) || run(d, DB_READY, stop) || (db_state(d) == DB_BUSY && (db_cancel(d) || run(d, DB_READY, 0)));
}
static int is(Db *d, const char *sql, const char *want) { return !batch(d, sql, 0) && !strcmp(db_out(d)->s ? db_out(d)->s : "", want); }

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
	static char sql[1 << 20];
	sql[fread(sql, 1, sizeof sql - 1, stdin)] = 0;
	Db d;
	double t0 = now(), t1, first = 0;
	int r = !pass || login(&d, pg, trust);
	if (!r && !(t1 = now(), r = db_query(&d, sql)))
		while (!r && db_state(&d) != DB_READY) {
			db_out(&d)->n = 0;
			r = run(&d, DB_READY, db_rows(&d) + 1);
			if (db_rows(&d) && !first) first = now();
			if (!quiet) fwrite(db_out(&d)->s, 1, db_out(&d)->n, stdout);
			if (!r && stop && db_rows(&d) >= stop && db_state(&d) == DB_BUSY) {
				nanosleep(&(struct timespec){0, 300000000}, NULL);
				uint64_t at = db_rows(&d);
				double c0 = now();
				db_out(&d)->n = 0;
				r = db_cancel(&d) || run(&d, DB_READY, 0);
				fprintf(stderr, "cancel %.2f rows_after_cancel %llu\n", now() - c0, (unsigned long long)(db_rows(&d) - at));
				if (!quiet) fwrite(db_out(&d)->s, 1, db_out(&d)->n, stdout);
			}
		}
	if (r) fprintf(stderr, "test_sql: %s\n", pass ? db_net(&d)->err : "no password");
	else fprintf(stderr, "login %.2f first_row %.2f total %.2f rows %llu\n", t1 - t0, first ? first - t1 : 0, now() - t1, (unsigned long long)db_rows(&d));
	if (pass) db_close(&d);
	return r ? 1 : 0;
}

/* a server's password and a login to it, or a clear line why not */
static int up(Db *d, int pg)
{
	const char *env = pg ? "SQL_PG_PASSWORD" : "SQL_SA_PASSWORD";
	if (!(pass = getenv(env))) { fprintf(stderr, "test_sql: %s is not set: start the test servers as src/sql/README.md says\n", env); return 0; }
	if (!login(d, pg, 1)) return 1;
	fprintf(stderr, "test_sql: no %s on 127.0.0.1:%d (%s): start it as src/sql/README.md says\n", pg ? "PostgreSQL" : "SQL Server", pg ? 5432 : 1433, db_net(d)->err);
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
		CHECK(login(&d, pg, 1) && strstr(db_net(&d)->err, why));
		db_close(&d);
	}
	pass = good;
	CHECK(login(&d, pg, 0) && strstr(db_net(&d)->err, "certificate verify failed"));
	db_close(&d);
}

/* SCRAM-SHA-256 without a server: pg_parse fed the server's messages for a
   client nonce of RFC 7677's example ("n=," since Postgres ignores the
   name; the expected proofs computed with Python's hashlib) */
static int scram(const char *server_first, const char *server_final)
{
	Pg p = { .net.fd = -1, .kill.fd = -1, .state = PG_AUTH };
	strcpy(p.nonce, "rOprNGfwEbeRWgbNEkqO"), strcpy(p.first, "n=,r=rOprNGfwEbeRWgbNEkqO"), strcpy(p.pass, "pencil");
	const char *m[2] = {server_first, server_final};
	for (int i = 0; i < 2 && p.state != PG_DEAD; i++) {
		size_t n = strlen(m[i]);
		char h[9] = {'R', 0, 0, (char)((n + 8) / 256), (char)(n + 8), 0, 0, 0, (char)(11 + i)};
		CHECK(!str_add(&p.in, h, 9) && !str_add(&p.in, m[i], n));
		pg_parse(&p);
		if (i == 0 && p.state != PG_DEAD) CHECK(p.net.snd.n > 5 && strstr(p.net.snd.s + 5, "c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,p=qvT2SWdEH5Q06albL+hjSYuUhCG7VndFyzIb7CK4n9k=") && !p.pass[0]);
	}
	int ok = p.state != PG_DEAD;
	pg_close(&p);
	return ok;
}

static void oracle(const char *py, const char *argv0)
{
	char cmd[4096];
	snprintf(cmd, sizeof cmd, "python3 %s %s %s", py, argv0, getenv("SEED") ? getenv("SEED") : "1");
	CHECK(!system(cmd));
}

int main(int argc, char **argv)
{
	if (argc > 1) return pass = getenv("SQL_SA_PASSWORD"), driver(argc, argv);
	Db d;
	if (!up(&d, 0)) return 1;
	CHECK(d.t.pkt == 4096 && !d.t.login.s);   /* LOGIN7, with the password, not kept */
	/* a child the editor starts does not hold the session open (the review of PR 133) */
	CHECK(fcntl(db_net(&d)->fd, F_GETFD) & FD_CLOEXEC);
	/* SSMS's session options */
	CHECK(is(&d, "select @@options o", "o\n5496\n(1 row affected)\n"));
	/* text in both directions: outside the BMP, and Latin-1 */
	CHECK(is(&d, "select N'\U0001F600é' x", "x\n\U0001F600é\n(1 row affected)\n"));
	/* a message's text as it is; a value's escaped, one per cell */
	CHECK(is(&d, "print 'a' + char(9) + 'b' + char(10) + 'c'; select 'a' + char(9) + 'b' + char(10) + 'c' x", "a\tb\nc\nx\na\\tb\\nc\n(1 row affected)\n"));
	/* a column name escaped as a value is */
	CHECK(is(&d, "select 1 as [a\tb]", "a\\tb\n1\n(1 row affected)\n"));
	/* a cancel while no batch runs sends nothing */
	CHECK(!db_cancel(&d) && db_state(&d) == DB_READY && !db_pending(&d));
	/* a cancel after the batch's last DONE was sent: the server still sends
	   DONE_ATTN, which must end this run, not the next (SqlRun.tla's
	   "firstdone").  The batch is sent and its whole reply waits unread
	   before the cancel goes, so the DONE_ATTN comes in a later read (sent
	   together, the plant was caught 0 of 1 times) */
	db_out(&d)->n = 0;
	CHECK(!db_query(&d, "select 1 as one") && !db_io(&d, 0) && !nanosleep(&(struct timespec){0, 200000000}, NULL));
	CHECK(db_state(&d) == DB_BUSY && !db_cancel(&d) && !run(&d, DB_READY, 0));
	CHECK(is(&d, "select 2 as two", "two\n2\n(1 row affected)\n"));
	/* a cancelled result reads on to the server's DONE_ATTN and shows no
	   error of its own; the connection then runs the next batch */
	CHECK(!batch(&d, "select top 2000000 a.object_id from sys.all_objects a, sys.all_objects b", 1000));
	CHECK(db_state(&d) == DB_READY && !strstr(db_out(&d)->s, "Msg") && d.t.rows >= 1000 && d.t.rows < 2000000);
	CHECK(is(&d, "select 1 as one", "one\n1\n(1 row affected)\n"));
	/* a batch ending in EXEC ends with DONEPROC, not DONE (the review of
	   PR 133); cancelling a running procedure */
	CHECK(is(&d, "exec sp_executesql N'select 1 a'", "a\n1\n(1 row affected)\n"));
	CHECK(!batch(&d, "exec sp_executesql N'select top 2000000 a.object_id from sys.all_objects a, sys.all_objects b'", 1000));
	CHECK(db_state(&d) == DB_READY && !strstr(db_out(&d)->s, "Msg") && d.t.rows < 2000000);
	CHECK(is(&d, "select 1 as one", "one\n1\n(1 row affected)\n"));
	/* a transaction's descriptor comes from the server and goes back with each batch */
	CHECK(!batch(&d, "begin tran", 0) && d.t.txn);
	CHECK(is(&d, "select @@trancount n", "n\n1\n(1 row affected)\n"));
	CHECK(!batch(&d, "rollback", 0) && !d.t.txn);
	db_close(&d);
	refusals(0, "Login failed for user 'sa'");
	oracle("tests/test_sql_tds.py", argv[0]);

	if (!up(&d, 1)) return 1;
	/* text in both directions; a message's text as it is, a value's escaped */
	CHECK(is(&d, "select '\U0001F600é' x", "x\n\U0001F600é\n(SELECT 1)\n"));
	CHECK(is(&d, "do $$ begin raise notice 'a\tb'; end $$; select E'a\\tb' x", "NOTICE 00000: a\\tb\n(DO)\nx\na\\tb\n(SELECT 1)\n"));
	/* a cancel while no query runs sends nothing */
	CHECK(!db_cancel(&d) && db_state(&d) == DB_READY && !db_pending(&d));
	/* a cancelled query shows no error of its own (57014 is ours); the
	   connection then runs the next */
	CHECK(!batch(&d, "select g from generate_series(1, 20000000) g", 1000));
	CHECK(db_state(&d) == DB_READY && !strstr(db_out(&d)->s, "ERROR") && d.g.rows >= 1000 && d.g.rows < 20000000);
	CHECK(is(&d, "select 1 as one", "one\n1\n(SELECT 1)\n"));
	/* a cancel is done once the server closes its connection, so the next
	   query is never the one cancelled (the review of PR 133: 16 of 20
	   were); the reply waits unread 20 ms, as there */
	for (int i = 0; i < 20; i++) {
		db_out(&d)->n = 0;
		CHECK(!db_query(&d, "select 1") && !db_io(&d, 0) && !nanosleep(&(struct timespec){0, 20000000}, NULL));
		CHECK(!db_cancel(&d) && !run(&d, DB_READY, 0));
		CHECK(is(&d, "select pg_sleep(0.05), 2 x", "pg_sleep\tx\n\t2\n(SELECT 1)\n"));
	}
	/* the transaction state comes from the server's ReadyForQuery */
	CHECK(!batch(&d, "begin", 0) && d.g.txn == 'T');
	CHECK(!batch(&d, "select 1/0", 0) && d.g.txn == 'E' && strstr(db_out(&d)->s, "ERROR 22012: division by zero"));
	CHECK(!batch(&d, "rollback", 0) && d.g.txn == 'I');
	/* the server's error 57014 when no cancel of ours runs (a timeout) is shown */
	CHECK(!batch(&d, "set statement_timeout = 10; select pg_sleep(1)", 0) && strstr(db_out(&d)->s, "ERROR 57014"));
	CHECK(!batch(&d, "set statement_timeout = 0", 0));
	/* a column name escaped as a value is */
	CHECK(is(&d, "select 1 as \"a\tb\"", "a\\tb\n1\n(SELECT 1)\n"));
	CHECK(!d.g.pass[0]);   /* the password is not kept past the login */
	db_close(&d);
	refusals(1, "password authentication failed for user \"postgres\"");
	/* SCRAM: the right proof is sent and the server's checked; a wrong
	   server signature and a nonce that does not extend ours refused */
	const char *sf = "r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096";
	CHECK(scram(sf, "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOg="));
	CHECK(!scram(sf, "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOh="));
	CHECK(!scram("r=xOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096", "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOg="));
	/* the host name: with the test server's own certificate trusted, a
	   checked login to 127.0.0.1 still fails, on the name */
	char ca[] = "/tmp/sql_caXXXXXX", cmd[512];
	int fd = mkstemp(ca);
	snprintf(cmd, sizeof cmd, "openssl s_client -starttls postgres -connect 127.0.0.1:5432 </dev/null 2>/dev/null | sed -n '/BEGIN CERT/,/END CERT/p' > %s", ca);
	CHECK(fd >= 0 && !close(fd) && !system(cmd));
	setenv("SSL_CERT_FILE", ca, 1);
	CHECK(login(&d, 1, 0) && strstr(db_net(&d)->err, "mismatch"));
	db_close(&d);
	unsetenv("SSL_CERT_FILE");
	unlink(ca);
	oracle("tests/test_sql_pg.py", argv[0]);
	return 0;
}
