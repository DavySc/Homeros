/* Q2: the catalog through sql_catalog.  usage: q2 MODE [args]
   list CONN            : every row of CONN's catalog (first 12 and the count), and how long it took
   cs                   : database cs (Latin1_General_CS_AS) on SQL Server
   share                : two buffers on one connection, :Connect in the second and in the owner
   fail                 : wrong password / closed port: retried?
   dns HOST             : how long the first call takes for HOST
   big CONN             : longest loop turn and the call's time on a large catalog
   early                : Alt+Break before any F5, with and without a catalog call
   null                 : fn NULL while it loads */
#include <time.h>
#include "h.h"
static long long now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static int nrow, show;
static void row(void *ctx, const char *s, const char *t, const char *c)
{
	(void)ctx;
	if (nrow++ < show) printf("  [%s] [%s] [%s]\n", s, t, c);
}
static int cat(void) { nrow = 0; return sql_catalog(&e, e.buf, row, NULL); }
static int wait_cat(int ms) { int n = 0; for (int i = 0; i < ms / 10 && !(n = cat()); i++) loop_once(10); return n; }
int main(int argc, char **argv)
{
	setup();
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD"), *m = argc > 1 ? argv[1] : "";
	char conf[4096], f[8192];
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1:5432 postgres postgres trust pass=%s\n"
		"cs mssql 127.0.0.1 sa cs trust pass=%s\nbig mssql 127.0.0.1 sa big trust pass=%s\nbigpg pg 127.0.0.1 postgres big trust pass=%s\n"
		"wrong mssql 127.0.0.1 sa master trust pass=nope\nwrongpg pg 127.0.0.1 postgres postgres trust pass=nope\nclosed mssql 127.0.0.1:1 sa master trust pass=x\n"
		"late mssql 127.0.0.1:1434 sa master trust pass=%s\ndns mssql %s sa master trust pass=x\ndir %s/ms ms\ndir %s/pg pg\n", sa, pg, sa, sa, pg, sa, argc > 2 ? argv[2] : "x", dir, dir);
	put(conf, f, 0600);
	put("ms/a.sql", "select 1 as one\n", 0600); put("ms/b.sql", "select 2 as two\n", 0600); put("x.sql", "select 1\n", 0600);
	core_open(&e, "x.sql");
	if (!strcmp(m, "list") || !strcmp(m, "big")) {
		sql_connect(&e, argv[2]);
		show = !strcmp(m, "list") ? 12 : 0;
		long long t0 = now(), worst = 0, t;
		int n = 0;
		for (int i = 0; i < 6000 && !(n = sql_catalog(&e, e.buf, NULL == row ? NULL : row, NULL)); i++) { nrow = 0; t = now(); loop_once(10); t = now() - t; if (t > worst) worst = t; }
		printf("%s: %d rows after %.0f ms; longest loop turn %.1f ms\n", argv[2], n, (double)(now() - t0) / 1e6, (double)worst / 1e6);
		show = 0;
		long long best = 1LL << 60;
		for (int i = 0; i < 20; i++) { t = now(); cat(); t = now() - t; if (t < best) best = t; }
		printf("one sql_catalog call once read: %.3f ms (best of 20)\n", (double)best / 1e6);
	}
	if (!strcmp(m, "cs")) { sql_connect(&e, "cs"); show = 5; printf("cs: %d rows\n", wait_cat(3000)); }
	if (!strcmp(m, "share")) {
		core_edit_file(&e, "ms/a.sql", 0);
		printf("A reads: %d\n", wait_cat(3000));
		system("docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -Q \"drop table if exists zshare; create table zshare (q int)\" >/dev/null");
		core_edit_file(&e, "ms/b.sql", 0);
		int n0 = cat();
		printf("B (same connection): %d at once\n", n0);
		printf("B :Connect ms -> %d; ", sql_connect(&e, "ms")); int n1 = cat(); printf("B's next call: %d at once (%s)\n", n1, n1 == n0 ? "old catalog kept" : "dropped");
		core_edit_file(&e, "ms/a.sql", 0);
		printf("A :Connect pg -> %d; ", sql_connect(&e, "pg")); printf("A: %d after wait\n", wait_cat(3000));
		core_edit_file(&e, "ms/b.sql", 0);
		n1 = cat(); printf("B after the owner left: %d at once, %d after wait\n", n1, wait_cat(3000));
		system("docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -Q \"drop table if exists zshare\" >/dev/null");
	}
	if (!strcmp(m, "fail")) {
		const char *c[] = { "wrong", "wrongpg", "closed" };
		for (int i = 0; i < 3; i++) {
			sql_connect(&e, c[i]); strcpy(e.msg, "kept");
			int n = wait_cat(2000);
			printf("%s: %d rows after 2 s, msg=[%s]\n", c[i], n, e.msg);
		}
		/* the server comes back (the password fixed): does the catalog try again? */
		snprintf(f, sizeof f, "wrong mssql 127.0.0.1 sa master trust pass=%s\n", sa); put(conf, f, 0600);
		core_edit_file(&e, "ms/a.sql", 0); sql_connect(&e, "wrong");
		sql_key(&e, KEY_F5); until("Completion time");
		printf("after the file is fixed, in another buffer on that name, F5 works: [%.20s]; catalog: %d rows after 2 s\n", results(), wait_cat(2000));
	}
	if (!strcmp(m, "dns")) {
		sql_connect(&e, "dns");
		long long t = now(); int n = cat(); t = now() - t;
		printf("first sql_catalog call for host %s: %.0f ms (returned %d)\n", argv[2], (double)t / 1e6, n);
		t = now(); n = cat(); t = now() - t;
		printf("second call: %.3f ms\n", (double)t / 1e6);
	}
	if (!strcmp(m, "early")) {
		core_edit_file(&e, "ms/a.sql", 0);
		printf("Alt+Break before anything: %d\n", sql_key(&e, KEY_BREAK));
		e.msg[0] = 0; sql_connect(&e, ""); printf(":Connect before: [%s]\n", e.msg);
		cat();
		printf("Alt+Break after one catalog call: %d\n", sql_key(&e, KEY_BREAK));
		e.msg[0] = 0; sql_connect(&e, ""); printf(":Connect after: [%s]\n", e.msg);
	}
	if (!strcmp(m, "late")) {   /* the server unreachable at the first call, reachable later */
		core_edit_file(&e, "ms/a.sql", 0); sql_connect(&e, "late");
		printf("server down: %d rows after 1 s\n", wait_cat(1000));
		system("python3 /tmp/rv/p/fwd.py 1434 1433 & sleep 0.5");
		sql_key(&e, KEY_F5); until("Completion time");
		printf("server up, F5 works: [%.5s]; catalog: %d rows after 2 s more\n", results(), wait_cat(2000));
		sql_catalog(&e, e.buf, NULL, NULL); printf("after fn NULL: %d rows\n", wait_cat(2000));
		system("pkill -f fwd.py");
	}
	if (!strcmp(m, "null")) {
		core_edit_file(&e, "ms/a.sql", 0);
		for (int k = 0; k < 40; k++) { cat(); for (int i = 0; i < k; i++) loop_once(1); sql_catalog(&e, e.buf, NULL, NULL); }
		printf("after 40 drops mid-load: %d rows\n", wait_cat(3000));
	}
	if (!strcmp(m, "nocon")) {
		long long t = now();
		for (int i = 0; i < 10000; i++) cat();
		printf("10,000 calls with no connection for the folder: %.1f us each\n", (double)(now() - t) / 1e3 / 10000);
	}
	return 0;
}
