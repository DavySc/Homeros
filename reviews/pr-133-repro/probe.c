/* review probes for PR 133 */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "src/sql/pg.h"
#include "src/sql/tds.h"

typedef struct { int pg; Tds t; Pg g; int *state; uint64_t *rows; Str *out; Net *net, *kill; } Db;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int run(Db *d, int until, int ms)
{
	double end = now() + ms;
	while (*d->state != until && *d->state != TDS_DEAD) {
		int w = d->pg ? pg_pending(&d->g) : tds_pending(&d->t);
		struct pollfd p[2] = {{d->net->fd, (short)(POLLIN | (w ? POLLOUT : 0)), 0}, {d->kill ? d->kill->fd : -1, POLLOUT, 0}};
		int left = (int)(end - now());
		if (left <= 0) return -2;
		if (poll(p, 2, left) < 0) return -1;
		int in = p[0].revents & (POLLIN | POLLHUP | POLLERR);
		if ((d->pg ? pg_io(&d->g, in) : tds_io(&d->t, in)) < 0) return -1;
	}
	return *d->state == TDS_DEAD ? -1 : 0;
}
static int login(Db *d, int pg, const char *host, int port, const char *user, const char *pass, const char *db, int trust)
{
	*d = (Db){ .pg = pg, .state = pg ? &d->g.state : &d->t.state, .rows = pg ? &d->g.rows : &d->t.rows,
		.out = pg ? &d->g.out : &d->t.out, .net = pg ? &d->g.net : &d->t.net, .kill = pg ? &d->g.kill : NULL };
	return (pg ? pg_open(&d->g, host, port, user, pass, db, trust) : tds_open(&d->t, host, port, user, pass, db, trust)) || run(d, TDS_READY, 20000);
}
static int q(Db *d, const char *sql, int ms)
{
	d->out->n = 0;
	int r = (d->pg ? pg_query(&d->g, sql) : tds_batch(&d->t, sql));
	return r ? r : run(d, TDS_READY, ms);
}
static void show(Db *d, const char *tag, int r) { printf("[%s] r=%d state=%d err='%s' out=<<%.*s>>\n", tag, r, *d->state, d->net->err, (int)d->out->n, d->out->s ? d->out->s : ""); fflush(stdout); }
static void dbclose(Db *d) { d->pg ? pg_close(&d->g) : tds_close(&d->t); }

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";
	const char *sa = getenv("SQL_SA_PASSWORD"), *pgp = getenv("SQL_PG_PASSWORD");
	Db d, e;
	if (!strcmp(mode, "sql")) {   /* sql PG HOST PORT TRUST DB 'query'... */
		int pg = atoi(argv[2]);
		double t0 = now();
		int r = login(&d, pg, argv[3], atoi(argv[4]), pg ? "postgres" : "sa", pg ? pgp : sa, argv[6], atoi(argv[5]));
		printf("login %.0f ms ", now() - t0);
		show(&d, "login", r);
		for (int a = 7; a < argc && !r; a++) { r = q(&d, argv[a], 30000); show(&d, argv[a], r); }
		dbclose(&d);
		return r != 0;
	}
	if (!strcmp(mode, "leak")) {   /* leak PG: does a child keep the server session alive after close? */
		int pg = atoi(argv[2]);
		const char *cnt = pg ? "select count(*) from pg_stat_activity where application_name = 'homeros'" : "select count(*) n from sys.dm_exec_sessions where program_name = 'homeros'";
		if (login(&d, pg, "127.0.0.1", pg ? 5432 : 1433, pg ? "postgres" : "sa", pg ? pgp : sa, pg ? "postgres" : "master", 1)) return 1;
		show(&d, "before", q(&d, cnt, 5000));
		pid_t c = fork();
		if (!c) { execlp("sleep", "sleep", "20", (char *)0); _exit(1); }
		dbclose(&d);
		sleep(2);
		if (login(&e, pg, "127.0.0.1", pg ? 5432 : 1433, pg ? "postgres" : "sa", pg ? pgp : sa, pg ? "postgres" : "master", 1)) return 1;
		show(&e, "after close, child alive", q(&e, cnt, 5000));
		kill(c, 9); waitpid(c, NULL, 0); sleep(2);
		show(&e, "after child died", q(&e, cnt, 5000));
		dbclose(&e);
		return 0;
	}
	if (!strcmp(mode, "pgrace")) {   /* cancel sent as the query ends; the next query follows at once */
		int n = atoi(argv[2]), hit = 0;
		if (login(&d, 1, "127.0.0.1", 5432, "postgres", pgp, "postgres", 1)) return 1;
		for (int i = 0; i < n; i++) {
			d.out->n = 0;
			if (pg_query(&d.g, "select 1 as one") || pg_io(&d.g, 0)) return 2;
			nanosleep(&(struct timespec){0, 20000000}, NULL);   /* the reply waits unread */
			if (pg_cancel(&d.g) || run(&d, TDS_READY, 5000)) return 3;
			int r = q(&d, "select pg_sleep(0.3), 2 as two", 5000);
			if (r || strstr(d.out->s, "57014") || !strstr(d.out->s, "SELECT 1")) { hit++; if (hit == 1) show(&d, "next query after a late cancel", r); }
			if (r) return 4;
		}
		printf("pgrace: %d of %d next queries were cancelled by the previous run's cancel\n", hit, n);
		dbclose(&d);
		return 0;
	}

	if (!strcmp(mode, "cancelexec")) {   /* cancel a running EXEC after some rows; then the next batch */
		if (login(&d, 0, "127.0.0.1", 1433, "sa", sa, "tempdb", 1)) return 1;
		for (int i = 0; i < atoi(argv[2]); i++) {
			d.out->n = 0;
			if (tds_batch(&d.t, argv[3])) return 2;
			uint64_t r0 = d.t.rows;
			while (d.t.state == TDS_BUSY && d.t.rows < r0 + 1000) { struct pollfd p = {d.net->fd, POLLIN | (tds_pending(&d.t) ? POLLOUT : 0), 0}; poll(&p, 1, 300); if (tds_io(&d.t, p.revents & POLLIN) < 0) break; if (!p.revents) break; }
			int st = d.t.state, r = tds_cancel(&d.t);
			r = r ? r : run(&d, TDS_READY, 8000);
			printf("state at cancel %d, after: r=%d state=%d err='%s' msgs=%s; ", st, r, d.t.state, d.net->err, strstr(d.out->s ? d.out->s : "", "Msg") ? "yes" : "no");
			if (r) return 3;
			r = q(&d, "select 5 five", 5000);
			printf("next: r=%d out=%.*s\n", r, 6, d.out->s);
			if (r) return 4;
		}
		return 0;
	}
	if (!strcmp(mode, "kill")) {   /* another session kills ours while idle; then a batch */
		if (login(&d, 0, "127.0.0.1", 1433, "sa", sa, "master", 1) || login(&e, 0, "127.0.0.1", 1433, "sa", sa, "master", 1)) return 1;
		q(&d, "select @@spid s", 5000);
		char k[64]; snprintf(k, sizeof k, "kill %d", atoi(d.out->s + 2));
		show(&e, k, q(&e, k, 5000));
		sleep(1);
		double t0 = now();
		int r = q(&d, "select 1 as one", 15000);
		printf("%.0f ms: ", now() - t0); show(&d, "batch on a killed session", r);
		return 0;
	}

	if (!strcmp(mode, "pgrace2")) {   /* as pgrace, but the cancel waits for the server to close its connection (libpq's PQcancel) */
		int n = atoi(argv[2]), hit = 0;
		if (login(&d, 1, "127.0.0.1", 5432, "postgres", pgp, "postgres", 1)) return 1;
		for (int i = 0; i < n; i++) {
			d.out->n = 0;
			if (pg_query(&d.g, "select 1 as one") || pg_io(&d.g, 0)) return 2;
			nanosleep(&(struct timespec){0, 20000000}, NULL);
			struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(5432), .sin_addr.s_addr = htonl(0x7F000001) };
			int fd = socket(AF_INET, SOCK_STREAM, 0);
			unsigned char m[16] = {0, 0, 0, 16, 0x04, 0xD2, 0x16, 0x2E, d.g.pid >> 24, d.g.pid >> 16, d.g.pid >> 8, d.g.pid, d.g.key >> 24, d.g.key >> 16, d.g.key >> 8, d.g.key}, x;
			if (connect(fd, (struct sockaddr *)&a, sizeof a) || write(fd, m, 16) != 16) return 5;
			while (read(fd, &x, 1) > 0) ;   /* until the server closes: the cancel has been processed */
			close(fd);
			if (run(&d, TDS_READY, 5000)) return 3;
			int r = q(&d, "select pg_sleep(0.3), 2 as two", 5000);
			if (r || strstr(d.out->s, "57014") || !strstr(d.out->s, "SELECT 1")) hit++;
			if (r) return 4;
		}
		printf("pgrace2: %d of %d next queries were cancelled by the previous run's cancel\n", hit, n);
		dbclose(&d);
		return 0;
	}
	if (!strcmp(mode, "spurious")) {   /* readable reported, nothing there yet */
		tds_open(&d.t, "127.0.0.1", 1433, "sa", sa, "master", 1);
		printf("tds_io(readable=1) before any byte: %d\n", tds_io(&d.t, 1));
		pg_open(&d.g, "127.0.0.1", 5432, "postgres", pgp, "postgres", 1);
		printf("pg_io(readable=1) before any byte: %d\n", pg_io(&d.g, 1));
		return 0;
	}
	if (!strcmp(mode, "stall")) {   /* stall PG SECONDS: run a query after the caller has black-holed the port */
		int pg = atoi(argv[2]);
		if (login(&d, pg, "127.0.0.1", pg ? 5432 : 1433, pg ? "postgres" : "sa", pg ? pgp : sa, pg ? "postgres" : "master", 1)) return 1;
		printf("logged in; black-hole now\n"); fflush(stdout);
		sleep(5);
		double t0 = now();
		int r = q(&d, "select 1 as one", atoi(argv[3]) * 1000);
		printf("after %.0f s: ", (now() - t0) / 1e3); show(&d, "query on a dead path", r);
		if (r == -2) { r = pg ? pg_cancel(&d.g) : tds_cancel(&d.t); r = r ? r : run(&d, TDS_READY, 10000); printf("cancel then 10 s: "); show(&d, "cancel", r); }
		return 0;
	}
	return 2;
}
