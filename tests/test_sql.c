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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "../src/sql/db.h"
#ifdef _WIN32   /* src/sql/check.sh: the same test, built for Windows, under Wine */
#include <io.h>
#include <windows.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <poll.h>
#include <sys/socket.h>
#endif

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
#ifdef _WIN32   /* the sockets' events: every wake reads (net.h) */
		HANDLE h[2] = {(HANDLE)(INT_PTR)db_net(d)->fd, k ? (HANDLE)(INT_PTR)k->fd : NULL};
		if (WaitForMultipleObjects(h[1] ? 2 : 1, h, FALSE, INFINITE) == WAIT_FAILED) return -1;
		int in = 1;
#else
		struct pollfd p[2] = {{db_net(d)->fd, (short)(POLLIN | (db_pending(d) ? POLLOUT : 0)), 0}, {k ? k->fd : -1, (short)(POLLIN | (k && k->snd.n ? POLLOUT : 0)), 0}};
		if (poll(p, 2, -1) < 0) return -1;
		int in = p[0].revents & (POLLIN | POLLHUP | POLLERR);
#endif
		if (db_io(d, in) < 0) return -1;
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
#ifdef _WIN32   /* the oracles read the bytes the server sent */
	_setmode(0, _O_BINARY), _setmode(1, _O_BINARY);
#endif
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
	for (int i = 0; i < 2; i++) {   /* twice: Windows's CAs are read once */
		CHECK(login(&d, pg, 0) && strstr(db_net(&d)->err, "certificate verify failed"));
		db_close(&d);
	}
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

#ifndef _WIN32   /* a socket pair stands in for the cancel connection; Windows's Net is an event */
/* Two cancels without a server, the second's connection closed first:
   the first's ReadyForQuery, come before its connection closed, must not
   end the second (racing for that order against a live server caught the
   plant here and missed it on CI) */
static void stale_ready(void)
{
	Pg p = { .net.fd = -1, .kill.fd = -1, .state = PG_CANCEL };
	for (int i = 0, s[2]; i < 2; i++) {
		CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, s) && !close(s[1]));   /* the server closed it */
		p.kill.fd = s[0];
		if (!i) CHECK(!str_add(&p.in, "Z\0\0\0\5I", 6) && !pg_parse(&p) && p.state == PG_CANCEL);
		CHECK(!pg_io(&p, 0) && p.kill.fd < 0 && p.state == (i ? PG_CANCEL : PG_READY));
		p.state = PG_CANCEL;
	}
	pg_close(&p);
}
#endif

/* Our own cancel without a server: the reply's tokens in one order every
   run (a live server's timing decided whether it sent the 3980 or a lone
   final DONE first, and the plants were missed 4 of 4 and 1 of 4 times) */
static void cancel_tokens(void)
{
	static const char done[] = "\xFD\0\0\0\0\0\0\0\0\0\0\0\0", attn[] = "\xFD\x20\0\0\0\0\0\0\0\0\0\0\0";
	/* 3980 "The request failed to run because the batch is aborted",
	   from aborting a procedure; message, server and procedure names empty */
	static const char err[] = "\xAA\x0E\0\x8C\x0F\0\0\1\x10\0\0\0\0\1\0\0\0";
	Tds t = { .net.fd = -1, .state = TDS_CANCEL, .quiet = 1 };
	/* a final DONE without the attention bit ends no cancel (SqlRun.tla's "firstdone") */
	CHECK(!str_add(&t.msg, done, 13) && !tds_parse(&t) && t.state == TDS_CANCEL);
	CHECK(!str_add(&t.msg, err, 17) && !str_add(&t.msg, attn, 13) && (t.eom = 1) && !tds_parse(&t));
	CHECK(t.state == TDS_READY && !t.quiet && !t.out.n);
	tds_close(&t);
}

#ifndef _WIN32   /* the raw socket is Windows's event handle there (net.h) */
/* A refused login's reason and the server's close_notify in one read: the
   reason comes first, the close is the next read's.  An in-process TLS
   server writes both before the client reads (against a live server it
   depended on whether they arrived together: train 27 missed the plant) */
static void close_notify(void)
{
	int l = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t al = sizeof a;
	CHECK(l >= 0 && !bind(l, (struct sockaddr *)&a, al) && !listen(l, 1) && !getsockname(l, (struct sockaddr *)&a, &al));
	Net c;
	CHECK(!net_open(&c, "127.0.0.1", ntohs(a.sin_port), 1));
	int f = accept(l, NULL, NULL);
	CHECK(f >= 0 && !fcntl(f, F_SETFL, O_NONBLOCK));   /* one thread: the server never waits */
	/* a throwaway P-256 key and self-signed certificate (the client trusts) */
	EVP_PKEY *k = EVP_EC_gen("P-256");
	X509 *x = X509_new();
	SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
	CHECK(k && x && ctx && X509_set_pubkey(x, k) && X509_gmtime_adj(X509_getm_notBefore(x), 0) && X509_gmtime_adj(X509_getm_notAfter(x), 60)
		&& X509_sign(x, k, EVP_sha256()) && SSL_CTX_use_certificate(ctx, x) == 1 && SSL_CTX_use_PrivateKey(ctx, k) == 1);
	SSL_CTX_set_num_tickets(ctx, 0);   /* nothing of the server's is on its way when it corks */
	SSL *sv = SSL_new(ctx);
	CHECK(sv && SSL_set_fd(sv, f));
	SSL_set_accept_state(sv);
	Str out = {0}, in = {0};
	int done = 0;
	for (int i = 0; i < 1000 && !(done && SSL_is_init_finished(sv)); i++) {
		char b[16384];
		ssize_t n = recv(c.fd, b, sizeof b, MSG_DONTWAIT);
		out.n = 0;
		if (!done) CHECK((done = net_shake(&c, b, n > 0 ? (size_t)n : 0, &out)) >= 0);
		CHECK(send(c.fd, out.s, out.n, 0) == (ssize_t)out.n);
		SSL_do_handshake(sv);
	}
	c.tls = 1;
	/* corked: both records leave as one segment, so they arrive together */
	CHECK(SSL_is_init_finished(sv) && !setsockopt(f, IPPROTO_TCP, TCP_CORK, &(int){1}, sizeof(int)) && SSL_write(sv, "reason", 6) == 6 && SSL_shutdown(sv) >= 0
		&& !setsockopt(f, IPPROTO_TCP, TCP_CORK, &(int){0}, sizeof(int)));
	CHECK(poll(&(struct pollfd){c.fd, POLLIN, 0}, 1, 1000) == 1);
	CHECK(!net_read(&c, &in) && in.n == 6 && !memcmp(in.s, "reason", 6));
	SSL_free(sv), SSL_CTX_free(ctx), X509_free(x), EVP_PKEY_free(k), close(f), close(l);
	CHECK(poll(&(struct pollfd){c.fd, POLLIN, 0}, 1, 1000) == 1 && net_read(&c, &in) && strstr(c.err, "closed"));
	str_free(&out), str_free(&in), net_close(&c);
}
#endif

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
	CHECK(d.t.pkt == 4096 && !d.t.login.s);   /* LOGIN7, with the password, not kept */
	/* a child the editor starts does not hold the session open, and a
	   read never blocks the editor (the review of PR 133; on Windows,
	   WSAEventSelect makes the socket non-blocking, and nothing reads it back) */
#ifdef _WIN32
	DWORD f;
	CHECK(GetHandleInformation((HANDLE)(INT_PTR)db_net(&d)->sock, &f) && !(f & HANDLE_FLAG_INHERIT));
#else
	CHECK(fcntl(db_net(&d)->fd, F_GETFD) & FD_CLOEXEC && fcntl(db_net(&d)->fd, F_GETFL) & O_NONBLOCK);
#endif
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
#ifndef _WIN32
	stale_ready();
	close_notify();
#endif
	cancel_tokens();
	CHECK(scram(sf, "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOg="));
	CHECK(!scram(sf, "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOh="));
	CHECK(!scram("r=xOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096", "v=3HO6Qt1M4MKJrmlKaoOqLAI0/0TV0HZe7J9H3MBtSOg="));
#ifndef _WIN32   /* Windows trusts its ROOT store, not SSL_CERT_FILE; the name check is the same code */
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
#endif
	oracle("tests/test_sql_pg.py", argv[0]);
#ifdef _WIN32   /* winpass.sh's summary line */
	fprintf(stderr, "test_sql: both servers: login, text, cancel, transactions, refusals\n");
#endif
	return 0;
}
