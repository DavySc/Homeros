/* PR 173 review probe: sql_catalog(table).
   p173 time CONN TABLE [N]   per-call times: table list, TABLE, upper-cased TABLE, a missing table
   p173 show CONN TABLE...    what each lookup returns (and the table count)
   p173 conn A B TABLE        :Connect from A to B: what TABLE's lookup gives at each step
   p173 nocon [N]             a .sql buffer with no connection: per-call time
   p173 two CONN TABLE        a second buffer on CONN, never logged in */
#include <time.h>
#include <ctype.h>
#include "h.h"
static char out[1 << 16];
static long cnt;
static void show(void *c, const char *s, const char *t, const char *col)
{
	(void)c; cnt++;
	if (strlen(out) < sizeof out - 600) snprintf(out + strlen(out), 600, col ? "%s.%s.%s " : "%s.%s ", s, t, col);
}
static void count(void *c, const char *s, const char *t, const char *col) { (void)c, (void)s, (void)t, (void)col; cnt++; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static void bench(const char *what, const char *table, int N)
{
	double *r = malloc(sizeof *r * (size_t)N);
	for (int i = 0; i < N; i++) { double t = now(); cnt = 0; sql_catalog(&e, e.buf, table, count, NULL); r[i] = now() - t; }
	qsort(r, (size_t)N, sizeof *r, cmp);
	printf("%-22s %7ld rows  median %.4f  p99 %.4f  max %.4f ms\n", what, cnt, r[N / 2], r[N * 99 / 100], r[N - 1]);
	free(r);
}
static int look(const char *table)
{
	*out = 0; cnt = 0;
	int n = sql_catalog(&e, e.buf, table, show, NULL);
	printf("  %-10s -> %d: %.300s\n", table ? table : "(NULL)", n, out);
	return n;
}
static int waitcat(void)
{
	int n = 0; double t = now();
	for (int i = 0; i < 12000 && !(n = sql_catalog(&e, e.buf, NULL, count, NULL)); i++) loop_once(10);
	printf("catalog: %d tables, %.0f ms after F5\n", n, now() - t);
	return n;
}
static void conf(void)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD");
	char c[4096], f[8192];
	mkdtemp(dir); chdir(dir); mkdir("homeros", 0700);
	setenv("XDG_CONFIG_HOME", dir, 1);
	snprintf(c, sizeof c, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\ncs mssql 127.0.0.1 sa cs trust pass=%s\nmsbig mssql 127.0.0.1 sa big trust pass=%s\n"
		"pg pg 127.0.0.1 postgres postgres trust pass=%s\npgbig pg 127.0.0.1 postgres big trust pass=%s\npgb pg 127.0.0.1 postgres hbench trust pass=%s\npgwide pg 127.0.0.1 postgres wide trust pass=%s\npgmulti pg 127.0.0.1 postgres multi trust pass=%s\n", sa, sa, sa, pg, pg, pg, pg, pg);
	put(c, f, 0600);
	put("a.sql", "select 1 as one\n", 0600);
	put("b.sql", "select 1 as one\n", 0600);
}
static void f5(void) { if (sql_key(&e, KEY_F5) != 1) printf("F5 refused: %s\n", e.msg); until("Completion time"); }
int main(int argc, char **argv)
{
	conf();
	if (core_open(&e, "a.sql")) return 1;
	const char *m = argv[1];
	if (!strcmp(m, "nocon")) {
		bench("no connection", "dom", argc > 2 ? atoi(argv[2]) : 2000);
		return 0;
	}
	if (sql_connect(&e, argv[2])) { printf("connect: %s\n", e.msg); return 1; }
	if (!strcmp(m, "time")) {
		int N = argc > 4 ? atoi(argv[4]) : 2000;
		char up[256]; size_t i = 0;
		for (; argv[3][i]; i++) up[i] = (char)toupper((unsigned char)argv[3][i]);
		up[i] = 0;
		f5(); waitcat();
		bench("table list (NULL)", NULL, N);
		bench(argv[3], argv[3], N);
		bench(up, up, N);
		bench("no such table", "nosuchtable_zz", N);
	} else if (!strcmp(m, "show")) {
		printf("before F5:\n"); look(argv[3]);
		f5(); waitcat();
		for (int i = 3; i < argc; i++) look(argv[i]);
	} else if (!strcmp(m, "conn")) {
		f5(); waitcat(); printf("on %s:\n", argv[2]); look(argv[4]);
		if (sql_connect(&e, argv[3])) printf("connect: %s\n", e.msg);
		printf("after :Connect %s, before F5:\n", argv[3]); look(argv[4]);
		for (int i = 0; i < 100; i++) loop_once(10);
		printf("1 s later, still no F5:\n"); look(argv[4]);
		f5(); waitcat(); printf("after F5 on %s:\n", argv[3]); look(argv[4]);
		sql_connect(&e, argv[2]);
		printf("back on %s, before F5:\n", argv[2]); look(argv[4]);
	} else if (!strcmp(m, "two")) {
		f5(); waitcat(); look(argv[3]);
		if (core_edit_file(&e, "b.sql", 0) || sql_connect(&e, argv[2])) printf("b.sql: %s\n", e.msg);
		printf("second buffer on %s, never logged in:\n", argv[2]); look(argv[3]);
		core_edit_file(&e, "a.sql", 0);
		sql_connect(&e, argv[2][0] == 'p' ? "pg" : "ms");
		core_edit_file(&e, "b.sql", 0);
		printf("first buffer moved away; second buffer:\n"); look(argv[3]);
	}
	return 0;
}
