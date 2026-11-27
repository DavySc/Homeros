/* PR 172 probes.  usage: p172 MODE CONN [N]
   CONN: none (no connections file line), ms, pg, cs, big, bigpg, dead (a host nothing answers on), dns HOST
   show   : the popup after "d." in a.sql (join dom d, the file names no column) before F5, after F5 in b.sql, and for "from " 
   time   : per-key times of a word start after a dot and of a plain word, N samples, before and after the login
   switch : two buffers on one connection: the loop turn that indexes after each switch
   off    : F5 against CONN, then keys while it tries */
#include <time.h>
#include "h.h"
#include "snip.h"
static long long now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static void feed(int k) { if (e.ks.pending || !snip_key(&e, k, 0)) keys_feed(&e, k, 0); }
static double tfeed(int k) { long long t = now(); feed(k); return (double)(now() - t) / 1e6; }
static void type(const char *s) { for (; *s; s++) feed(*s); }
static const char *pum(int max)
{
	static char b[1 << 16];
	size_t n = 0;
	b[0] = 0;
	for (int i = 0; i < e.npum && i < max && n < sizeof b - 300; i++) n += (size_t)sprintf(b + n, "%s ", e.pum[i].word);
	sprintf(b + n, "(%d)", e.npum);
	return b;
}
static void spin(int ms) { for (long long t = now(); now() - t < ms * 1000000LL;) loop_once(10); }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void stats(const char *what, double *v, int n)
{
	qsort(v, (size_t)n, sizeof *v, cmp);
	printf("%-34s median %.3f  p99 %.3f  max %.3f ms (n=%d)\n", what, v[n / 2], v[n * 99 / 100], v[n - 1], n);
}
static int nrow;
static void row(void *c, const char *s, const char *t, const char *k) { (void)c, (void)s, (void)t, (void)k; nrow++; }
static int wait_cat(int ms) { int n = 0; for (int i = 0; i < ms / 10 && !(n = sql_catalog(&e, e.buf, row, NULL)); i++) loop_once(10); return n; }
static void sample(const char *tag, int N)
{
	static double v[100000], w[100000], x[100000];
	/* cursor after "on d" at the end of a.sql; the index is current */
	spin(1300);
	for (int i = 0; i < N; i++) v[i] = tfeed('.'), x[i] = tfeed('k'), feed(KEY_BS), feed(KEY_BS);
	char b[200];
	snprintf(b, sizeof b, "%s: '.' after d", tag), stats(b, v, N);
	snprintf(b, sizeof b, "%s: 2nd letter after d.", tag), stats(b, x, N);
	feed(' '); spin(1300);
	for (int i = 0; i < N; i++) w[i] = tfeed('s'), feed(KEY_BS);
	snprintf(b, sizeof b, "%s: word start 's'", tag), stats(b, w, N);
	feed(KEY_BS);
}
int main(int argc, char **argv)
{
	setup();
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD"), *m = argv[1], *cn = argv[2];
	int N = argc > 3 && strcmp(cn, "dns") && strcmp(m, "conn") ? atoi(argv[3]) : 300;
	char conf[4096], f[8192], xdg[4096];
	snprintf(xdg, sizeof xdg, "%s/state", dir); setenv("XDG_STATE_HOME", xdg, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1:5432 postgres postgres trust pass=%s\n"
		"cs mssql 127.0.0.1 sa cs trust pass=%s\nbig mssql 127.0.0.1 sa big trust pass=%s\nbigpg pg 127.0.0.1 postgres big trust pass=%s\n"
		"b200 pg 127.0.0.1 postgres big200 trust pass=Homeros_Test1\nb500 pg 127.0.0.1 postgres big500 trust pass=Homeros_Test1\nb1000 pg 127.0.0.1 postgres big1000 trust pass=Homeros_Test1\ndead mssql 10.255.255.1 sa master trust pass=x\ndns mssql %s sa master trust pass=x\ndir %s/w %s\n", sa, pg, sa, sa, pg, argc > 3 ? argv[3] : "x", dir, cn);
	if (strcmp(cn, "none")) put(conf, f, 0600);
	mkdir("w", 0700);
	const char *tail = getenv("P172_TAIL") ? getenv("P172_TAIL") : "select * from app a join dom d on d";
	FILE *fa = fopen("w/a.sql", "w");
	if (getenv("P172_BODY")) { FILE *src = fopen(getenv("P172_BODY"), "r"); int c; while (src && (c = fgetc(src)) != EOF) fputc(c, fa); }
	fputs(tail, fa); fclose(fa);
	put("w/b.sql", "select 1 as one\n", 0600); put("w/c.sql", "select 2 as two from dom\n", 0600);
	core_open(&e, "w/a.sql");
	feed('G'), feed('A');
	if (!strcmp(m, "show")) {
		type("."); printf("before F5, d.      : %s\n", pum(40)); feed(KEY_ESC), feed('u');
		core_edit_file(&e, "w/b.sql", 0); sql_key(&e, KEY_F5);
		printf("catalog rows       : %d\n", wait_cat(20000));
		core_edit_file(&e, "w/a.sql", 0); feed('G'), feed('A');
		type("."); printf("after F5, d.       : %s\n", pum(40)); feed(KEY_ESC), feed('u'); feed('A');
		type("\rselect * from "); printf("after 'from '      : %s\n", pum(12)); type("d"); printf("after 'from d'     : %s\n", pum(12)); feed(KEY_ESC), feed('u'); feed('A');
		type("\rselect * from Dom where "); type("k"); printf("where k (one table): %s\n", pum(12)); feed(KEY_ESC), feed('u');
	}
	if (!strcmp(m, "time")) {
		sample("no login", N);
		feed(KEY_ESC);
		core_edit_file(&e, "w/b.sql", 0); sql_key(&e, KEY_F5);
		int n = wait_cat(30000);
		core_edit_file(&e, "w/a.sql", 0); feed('G'), feed('A');
		printf("catalog rows %d; first '.' after it arrived: %.2f ms\n", n, tfeed('.')); feed(KEY_BS);
		if (n) sample("catalog", N);
	}
	if (!strcmp(m, "switch")) {
		feed(KEY_ESC);
		core_edit_file(&e, "w/b.sql", 0); sql_key(&e, KEY_F5);
		printf("catalog rows %d\n", wait_cat(30000));
		static double v[1000];
		const char *fs[] = { "w/a.sql", "w/c.sql" };
		for (int i = 0; i < N; i++) {
			core_edit_file(&e, fs[i & 1], 0);
			feed('j');   /* a key in the new buffer arms the index */
			long long t = now(); loop_once(0); v[i] = (double)(now() - t) / 1e6;
			spin(30);
		}
		stats("loop turn after a buffer switch", v, N);
		/* and when the first key in the buffer starts a word at once */
		for (int i = 0; i < N; i++) {
			core_edit_file(&e, fs[i & 1], 0);
			feed('G'); feed('A'); v[i] = tfeed(' ') + tfeed('s'); feed(KEY_BS); feed(KEY_BS); feed(KEY_ESC);
		}
		stats("word typed at once after a switch", v, N);
	}
	if (!strcmp(m, "off")) {
		static double v[100000];
		int n = 0;
		double t0 = tfeed('.'); feed(KEY_BS);
		feed(KEY_ESC);
		long long t = now(); sql_key(&e, KEY_F5); double f5 = (double)(now() - t) / 1e6;
		feed('G'), feed('A');
		long long worst = 0;
		for (long long s = now(); now() - s < 4000000000LL && n < 100000;) { v[n++] = tfeed('.'); feed(KEY_BS); long long q = now(); loop_once(1); q = now() - q; if (q > worst) worst = q; }
		printf("'.' before F5 %.3f ms; F5 itself %.1f ms; longest loop turn %.1f ms\n", t0, f5, (double)worst / 1e6);
		stats("'.' while the login is tried", v, n);
		printf("results: %.200s\n", results());
	}
	if (!strcmp(m, "conn")) {   /* CONN then :Connect argv[3]: whose columns after d. */
		feed(KEY_ESC);
		sql_key(&e, KEY_F5); printf("%s rows %d; ", cn, wait_cat(20000));
		feed('G'), feed('A'); type("."); printf("d. on %s: %s\n", cn, pum(40)); feed(KEY_BS); feed(KEY_ESC);
		sql_connect(&e, argv[3]); sql_key(&e, KEY_F5); printf(":Connect %s, F5: rows %d; ", argv[3], wait_cat(20000));
		spin(1500);
		feed('G'), feed('A'); type("."); printf("d. now: %s\n", pum(40)); feed(KEY_BS); feed(KEY_ESC);
		core_edit_file(&e, "w/c.sql", 0); feed('j'); spin(100); core_edit_file(&e, "w/a.sql", 0);
		feed('G'), feed('A'); type("."); printf("d. after visiting another buffer: %s\n", pum(40));
	}
	return 0;
}
