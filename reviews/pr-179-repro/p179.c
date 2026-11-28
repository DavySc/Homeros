/* PR 179 review probe: the connections picker, the status line's
   connection and a server alone.
     p179 SCENARIO
   with SQL_SA_PASSWORD and SQL_PG_PASSWORD set and the test servers up
   (and, for spacedb/collate/long, the databases reviews/pr-179-repro/setup.sql makes).
   Keys go through keys_input, as main.c sends them.  Prints what the
   picker lists, what the screen shows and the message. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core.h"
#include "grid.h"
#include "keys.h"
#include "loop.h"
#include "sql.h"
#include "text.h"
#include "tools.h"

static Editor e;
static char res[1 << 16];
static Grid g;

static const char *results(void)
{
	Win *w = e.wins;
	while (w && !w->buf->nonumber) w = w->next;
	size_t z = w ? text_len(w->buf->text) : 0, a = z > sizeof res - 1 ? z - (sizeof res - 1) : 0;
	size_t n = w ? text_read(w->buf->text, a, res, z - a) : 0;
	res[n] = 0;
	return res;
}
static const char *until(const char *s)
{
	for (int i = 0; i < 1500 && !strstr(results(), s); i++) loop_once(10);
	return res;
}
static void put(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	fputs(s, f), fclose(f), chmod(path, 0600);
}
static void keys(const char *s) { for (; *s; s++) keys_input(&e, *s == '\r' ? KEY_ENTER : *s == '\033' ? KEY_ESC : (unsigned char)*s, 0); }
static void wait_ms(int ms) { for (int i = 0; i < ms / 10; i++) loop_once(10); }
static const char *screen(int rows)   /* the screen's rows, a line each */
{
	static char t[1 << 16];
	size_t n = 0;
	grid_resize(&g, 24, 100);
	core_draw(&e, NULL, &g), tools_draw(&e, &g);
	for (int r = 0; r < g.rows; r++) {
		if (rows && r < g.rows - rows) continue;
		for (int c = 0; c < g.cols; c++) { Cell *x = &g.c[r * g.cols + c]; if (x->len < 5) memcpy(t + n, x->ch, x->len), n += x->len; }
		while (n && t[n - 1] == ' ') n--;
		t[n++] = '\n';
	}
	return t[n] = 0, t;
}
static void items(const char *what)
{
	const char *n;
	printf("%s: picker items:", what);
	for (int i = 0; (n = sql_pick(&e, i)); i++) printf(" [%s]", n);
	printf("\n");
}
static void st(const char *what)
{
	const char *s = sql_status(&e, e.buf);
	printf("%s: status [%s] msg [%s]\n", what, s ? s : "(null)", e.msg);
}
static int waitfor(const char *item)   /* the picker's list holds item; ms waited, -1 never */
{
	for (int i = 0; i < 600; i++) {
		const char *n;
		for (int j = 0; (n = sql_pick(&e, j)); j++) if (!strcmp(n, item)) return i * 10;
		loop_once(10);
	}
	return -1;
}
static void open(const char *p, const char *s) { put(p, s); if (core_edit_file(&e, p, 0)) printf("open failed\n"); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }

int main(int argc, char **argv)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD"), *sc = argc > 1 ? argv[1] : "";
	char dir[512] = "/tmp/p179XXXXXX", conf[4096], f[8192], longn[251];
	if (!sa || !pg) return puts("passwords"), 1;
	mkdtemp(dir), chdir(dir), mkdir("homeros", 0700), mkdir("ms", 0700), mkdir("pg", 0700), mkdir("none", 0700), mkdir("dd", 0700);
	setenv("XDG_CONFIG_HOME", dir, 1), setenv("HOME", dir, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	memset(longn, 'n', 250), longn[250] = 0;
	snprintf(f, sizeof f, "# name server host user database\n"
		"ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1 postgres postgres trust pass=%s\n"
		"srv mssql 127.0.0.1 sa - trust pass=%s\npgs pg 127.0.0.1 postgres - trust pass=%s\n"
		"#old mssql 10.0.0.1 sa master pass=x\nhalf mssql\n%s mssql 127.0.0.1 sa - trust pass=%s\n"
		"dir ~/ms ms\ndir ~/pg pg\ndir ~/dd ms/hcs\n", sa, pg, sa, pg, !strcmp(sc, "long") ? longn : "longname", sa);
	put(conf, f);
	put("ms/first.sql", "select db_name() as d\n");
	core_open(&e, "ms/first.sql");

	if (!strcmp(sc, "first")) {   /* the status line before any key */
		st("no key yet"), printf("%s", screen(2));
		keys("j"), st("after j"), printf("%s", screen(2));
	} else if (!strcmp(sc, "list")) {   /* what the picker lists: comments, short lines */
		keys(":Connect\r"), items("fresh"), printf("%s", screen(0));
		keys("old\r"), st("picked #old");
	} else if (!strcmp(sc, "spacedb")) {   /* a database whose name holds a space */
		keys(":Connect\r"), keys("srv\r"), st("picked srv");
		printf("waited %d ms\n", waitfor("srv/my db")), items("after"), printf("%s", screen(0));
		keys("mydb"), printf("%s", screen(0)), keys("\r"), st("picked srv/my db");
		keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("after F5");
		keys(":Connect srv/my db\r"), st("typed :Connect srv/my db");
	} else if (!strcmp(sc, "pgspace")) {
		open("pg/a.sql", "select current_database() as d\n");
		keys(":Connect\r"), keys("pgs\r"), st("picked pgs");
		printf("waited %d ms\n", waitfor("pgs/My Db")), items("after");
		keys("MyDb\r"), st("picked");
		keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("after F5");
	} else if (!strcmp(sc, "term")) {   /* the terminal opened before the databases come */
		keys(":Connect\r"), keys("srv\r"), st("picked srv");
		keys_input(&e, KEY_CTRL_DOT, 0), keys("echo hi");
		printf("waited %d ms\n", waitfor("srv/master")), wait_ms(300);
		printf("%s", screen(0));
		keys("zz\r"), wait_ms(300), printf("after typing zz Enter:\n%s", screen(0)), st("then");
		keys_input(&e, KEY_CTRL_DOT, 0), printf("terminal shut:\n%s", screen(0));
	} else if (!strcmp(sc, "key")) {   /* a key before the databases come: no picker */
		keys(":Connect\r"), keys("srv\r"), keys("j");
		printf("waited %d ms\n", waitfor("srv/master")), wait_ms(300), printf("%s", screen(3));
	} else if (!strcmp(sc, "long")) {   /* a long connection name and a long database name: the item's buffer */
		char k[400];
		(void)k; printf("connect %d\n", sql_connect(&e, longn)), st("connect long");
		char w[300]; snprintf(w, sizeof w, "%s/master", longn);
		printf("waited %d ms\n", waitfor(w)), items("after");
	} else if (!strcmp(sc, "collate")) {   /* databases whose collation is not the server's, a contained one */
		const char *dbs[] = { "hjp", "hct", "my db", "hcs2" };
		for (int i = 0; i < 4; i++) {
			char k[100], w[100]; int n = 0;
			snprintf(k, sizeof k, "ms/%s", dbs[i]), snprintf(w, sizeof w, "ms/%s/master", dbs[i]);
			printf("connect %s: %d\n", k, sql_connect(&e, k));
			keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time"));
			printf("databases after %d ms\n", waitfor(w));
			for (int j = 0; j < 100 && !(n = sql_catalog(&e, e.buf, NULL, (void (*)(void *, const char *, const char *, const char *))abort, NULL)); j++) loop_once(10);
			st("now"), sql_disconnect(&e);
		}
		items("all");
	} else if (!strcmp(sc, "one")) {   /* one database's catalog read: does its query run there */
		char k[100]; snprintf(k, sizeof k, "ms/%s", argv[2]);
		printf("connect %s: %d\n", k, sql_connect(&e, k));
		keys_input(&e, KEY_F5, 0), until("Completion time");
		printf("databases after %d ms\n", waitfor("ms/master")), st("now");
	} else if (!strcmp(sc, "dir")) {   /* a dir line naming name/db; the empty database */
		open("dd/a.sql", "select db_name() as d\n"), keys("j"), st("dir ms/hcs");
		keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("after F5");
		keys(":Connect ms/\r"), st(":Connect ms/"), keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time"));
		keys(":Connect pg/\r"), st(":Connect pg/"), keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("after");
	} else if (!strcmp(sc, "nofile")) {   /* :Connect with no connections file, and in a folder with no dir line */
		open("none/a.sql", "select 1\n"), keys("j"), st("no dir line"), printf("%s", screen(2));
		unlink(conf), keys(":Connect\r"), st("no file"), printf("%s", screen(0));
	} else if (!strcmp(sc, "live")) {   /* which is live: two buffers, the picker's marks, a filter, Enter on a marked item */
		keys_input(&e, KEY_F5, 0), until("Completion time"), st("ms run");
		open("pg/a.sql", "select current_database() as d\n"), keys("j"), st("pg before"), keys_input(&e, KEY_F5, 0), until("Completion time"), st("pg run");
		keys(":Connect\r"), items("two live"), printf("%s", screen(0));
		keys("ms\r"), st("picked ms in the pg buffer"), printf("%s", screen(2));
		keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("after F5");
		keys(":Connect\r"), items("then");
		keys("\033\033");
	} else if (!strcmp(sc, "time")) {   /* a redraw's time, the status line's connection in it */
		int n = argc > 2 ? atoi(argv[2]) : 20000;
		keys("j");
		grid_resize(&g, 50, 200);
		for (int i = 0; i < 200; i++) core_draw(&e, NULL, &g);
		double t = now();
		for (int i = 0; i < n; i++) core_draw(&e, NULL, &g);
		printf("%.4f\n", (now() - t) * 1e3 / n);   /* microseconds per draw */
	} else if (!strcmp(sc, "again")) {   /* a server alone picked twice, and while its list is being read */
		keys(":Connect\r"), keys("srv\r"), keys(":Connect\r"), keys("srv\r"), st("twice");
		printf("waited %d ms\n", waitfor("srv/master")), wait_ms(200), printf("%s", screen(0));
		keys("\033\033"), keys_input(&e, KEY_F5, 0), printf("%s", until("Completion time")), st("srv live"), items("srv live");
	}
	return 0;
}
