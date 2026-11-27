#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "sql.h"
#include "text.h"
static Editor e;
static char res[1 << 20];
__attribute__((unused)) static Win *rwin(void) { Win *w = e.wins; while (w && !w->buf->nonumber) w = w->next; return w; }
__attribute__((unused)) static const char *results(void)
{
	Win *w = rwin();
	size_t z = w ? text_len(w->buf->text) : 0, a = z > sizeof res - 1 ? z - (sizeof res - 1) : 0;
	size_t n = w ? text_read(w->buf->text, a, res, z - a) : 0;
	res[n] = 0;
	return res;
}
__attribute__((unused)) static const char *until(const char *s)
{
	for (int i = 0; i < 3000 && !strstr(results(), s); i++) loop_once(10);
	if (!strstr(res, s)) fprintf(stderr, "TIMEOUT want: %s\n", s);
	return res;
}
__attribute__((unused)) static void put(const char *path, const char *s, int mode)
{
	FILE *f = fopen(path, "w");
	fputs(s, f); fclose(f); chmod(path, (mode_t)mode);
}
__attribute__((unused)) static int nwins(void) { int n = 0; for (Win *w = e.wins; w; w = w->next) n++; return n; }
static char dir[] = "/tmp/sqlprobeXXXXXX";
__attribute__((unused)) static void setup(void)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD");
	char conf[4096], f[4096];
	mkdtemp(dir); chdir(dir); mkdir("homeros", 0700); mkdir("ms", 0700); mkdir("pg", 0700);
	setenv("XDG_CONFIG_HOME", dir, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1:5432 postgres postgres trust pass=%s\ndir %s/ms ms\ndir %s/pg pg\n", sa, pg, dir, dir);
	put(conf, f, 0600);
}
__attribute__((unused)) static void keys(const char *s) { for (; *s; s++) keys_input(&e, *s == '\r' ? KEY_ENTER : *s == '\033' ? KEY_ESC : (unsigned char)*s, 0); }
