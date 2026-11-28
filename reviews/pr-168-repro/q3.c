/* Q3 (second pass, head 1cecd6c0: the catalog is read when a session logs in).  usage: q3 MODE [args]
   list CONN | big CONN : F5, then the catalog: its first rows, the count, how long, the longest loop turn
   share                : two buffers on one connection; :Connect in the second; the first to another connection
   null                 : fn NULL 40 times while it loads
   dns HOST             : sql_catalog before any F5 with a name server that does not answer (no lookup now)
   tab                  : names with a tab, a backslash and a space, both servers */
#include <time.h>
#include "h.h"
static long long now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static int nrow, show;
static const char *only;
static void row(void *ctx, const char *s, const char *t, const char *c)
{
	(void)ctx;
	if ((!only || !strcmp(t, only)) && nrow++ < show) printf("  [%s] [%s] [%s]\n", s, t, c);
}
static int cat(void) { nrow = 0; return sql_catalog(&e, e.buf, row, NULL); }
static int wait_cat(int ms) { int n = 0; for (int i = 0; i < ms / 10 && !(n = cat()); i++) loop_once(10); return n; }
static void f5(void) { sql_key(&e, KEY_F5); until("Completion time"); }
static void sh(const char *q) { char c[2048]; snprintf(c, sizeof c, "docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -Q \"%s\" >/dev/null", q); system(c); }
int main(int argc, char **argv)
{
	setup();
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD"), *m = argc > 1 ? argv[1] : "";
	char conf[4096], f[8192];
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1:5432 postgres postgres trust pass=%s\n"
		"cs mssql 127.0.0.1 sa cs trust pass=%s\nbig mssql 127.0.0.1 sa big trust pass=%s\nbigpg pg 127.0.0.1 postgres big trust pass=%s\n"
		"dns mssql %s sa master trust pass=x\ndir %s/ms ms\ndir %s/pg pg\n", sa, pg, sa, sa, pg, argc > 2 ? argv[2] : "x", dir, dir);
	put(conf, f, 0600);
	put("ms/a.sql", "select 1 as one\n", 0600); put("ms/b.sql", "select 2 as two\n", 0600); put("x.sql", "select 1\n", 0600); put("pg/a.sql", "select 1 as one\n", 0600);
	core_open(&e, "x.sql");
	if (!strcmp(m, "list") || !strcmp(m, "big")) {
		sql_connect(&e, argv[2]);
		show = !strcmp(m, "list") ? (argc > 3 ? 1000 : 8) : 0;
		printf("before F5: %d\n", wait_cat(300));
		long long t0 = now(), worst = 0, t;
		int n = 0;
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 6000 && !(n = cat()); i++) { t = now(); loop_once(10); t = now() - t; if (t > worst) worst = t; }
		printf("%s: %d rows %.0f ms after F5; longest loop turn %.1f ms\n", argv[2], n, (double)(now() - t0) / 1e6, (double)worst / 1e6);
	}
	if (!strcmp(m, "share")) {
		core_edit_file(&e, "ms/a.sql", 0); f5();
		int n0 = wait_cat(3000);
		printf("A after F5: %d\n", n0);
		sh("drop table if exists zshare; create table zshare (q int)");
		core_edit_file(&e, "ms/b.sql", 0);
		printf("B (same connection, no F5): %d at once\n", cat());
		sql_connect(&e, "ms");
		printf("B :Connect ms: %d at once; ", cat());
		core_edit_file(&e, "ms/a.sql", 0); printf("A meanwhile (still logged in): %d; ", cat());
		core_edit_file(&e, "ms/b.sql", 0); f5(); printf("B after its F5: %d\n", wait_cat(3000));
		core_edit_file(&e, "ms/a.sql", 0); sql_connect(&e, "pg"); f5(); printf("A :Connect pg, F5: %d; ", wait_cat(3000));
		core_edit_file(&e, "ms/b.sql", 0); printf("B still: %d at once\n", cat());
		sh("drop table if exists zshare");
	}
	if (!strcmp(m, "null")) {
		core_edit_file(&e, "ms/a.sql", 0); f5(); wait_cat(3000);
		for (int k = 0; k < 40; k++) { sql_catalog(&e, e.buf, NULL, NULL); for (int i = 0; i < k; i++) loop_once(1); }
		printf("after 40 fn NULLs mid-load: %d rows\n", wait_cat(3000));
	}
	if (!strcmp(m, "dns")) {
		sql_connect(&e, "dns");
		long long t = now(); int n = cat(); t = now() - t;
		printf("sql_catalog before F5, host %s: %.3f ms (returned %d)\n", argv[2], (double)t / 1e6, n);
		t = now(); sql_catalog(&e, e.buf, NULL, NULL); t = now() - t;
		printf("fn NULL before F5: %.3f ms\n", (double)t / 1e6);
	}
	if (!strcmp(m, "tab")) {
		const char *files[] = { "ms/a.sql", "pg/a.sql" };
		for (int i = 0; i < 2; i++) {
			core_edit_file(&e, files[i], 0);
			text_delete(e.buf->text, 0, text_len(e.buf->text));
			const char *q = i ? "drop table if exists ztab; create table ztab (\"a\tb\" int, \"c\\d\" int, \"e f\" int, \"g\\\\t\" int, \"h\\\" int)\n"
			                  : "drop table if exists ztab\ncreate table ztab ([a\tb] int, [c\\d] int, [e f] int, [g\\\\t] int, [h\\] int)\n";
			text_insert(e.buf->text, 0, q, strlen(q));
			f5(); wait_cat(3000);
			sql_catalog(&e, e.buf, NULL, NULL);
			only = "ztab", show = 9; printf("%s: %d rows in all\n", files[i], wait_cat(3000));
		}
	}
	return 0;
}
