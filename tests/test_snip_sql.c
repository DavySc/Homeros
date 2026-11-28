/* The popup's names from the buffer's connection (sql_catalog), as
   SSMS's IntelliSense: none before a login, after one a table's columns
   after Q., the tables after FROM and schema., the statement's tables'
   columns elsewhere, in buffers that never name them; none after :Connect
   to a connection not logged in.  Needs the test PostgreSQL server
   (test_sql.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "snip.h"
#include "sql.h"

static Editor e;

static void feed(int k) { if (e.ks.pending || !snip_key(&e, k, 0)) keys_feed(&e, k, 0); }   /* as main.c */

static void put(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	CHECK(f && fputs(s, f) >= 0 && !fclose(f) && !chmod(path, 0600));
}

/* the popup after keys k typed at the end of path's text (NULL: the
   current buffer's) */
static const char *after(const char *path, const char *k)
{
	static char b[4096];
	CHECK(!path || !core_edit_file(&e, path, 0));
	feed('G'), feed('A');
	for (; *k; k++) feed(*k);
	b[0] = 0;
	for (int i = 0; i < e.npum; i++) strcat(strcat(b, e.pum[i].word), " ");
	feed(KEY_ESC), feed('u');   /* unchanged again, so the buffer can be left */
	return b;
}

int main(void)
{
	const char *pg = getenv("SQL_PG_PASSWORD");
	if (!pg) { fprintf(stderr, "test_snip_sql: SQL_PG_PASSWORD: start the test server as src/sql/README.md says\n"); return 1; }
	char dir[] = "/tmp/snip_sqlXXXXXX", f[1024];
	CHECK(mkdtemp(dir) && !chdir(dir) && !mkdir("homeros", 0700));
	setenv("XDG_CONFIG_HOME", dir, 1);
	snprintf(f, sizeof f, "pg pg 127.0.0.1 postgres postgres trust pass=%s\npg2 pg 127.0.0.1 postgres template1 trust pass=%s\ndir %s pg\n", pg, pg, dir);
	put("homeros/sql", f);
	put("make.sql", "drop table if exists scat; create table scat (k int, zone int, qty_left int); drop table if exists scat_only; create table scat_only (v int)\n");
	put("q.sql", "select * from app a join scat d on d");
	put("r.sql", "select * from scat where");
	CHECK(!core_open(&e, "q.sql"));
	CHECK(!strcmp(after("q.sql", "."), ""));   /* no login yet: the file has no columns of scat */
	CHECK(!core_edit_file(&e, "make.sql", 0) && sql_key(&e, KEY_F5) == 1);
	int n = 0;
	for (int i = 0; i < 300 && !strstr(after("q.sql", "."), "zone"); i++) loop_once(10), n = i;
	CHECK(!strcmp(after("q.sql", "."), "k zone qty_left "));
	/* an edit and a pause make the index again; the catalog stays in it */
	CHECK(!core_edit_file(&e, "q.sql", 0));
	feed('O');
	for (const char *k = "select 1 from other_one"; *k; k++) feed(*k);
	feed(KEY_ESC);
	for (long long t = loop_now(); loop_now() - t < 1200000000LL;) loop_once(100);
	CHECK(!strcmp(after(NULL, "."), "k zone qty_left "));
	feed('u');
	/* another buffer on the connection, and a switch back */
	CHECK(strstr(after("r.sql", " qty"), "qty_left "));
	CHECK(!strcmp(after(NULL, "\nfrom scat_o"), "scat_only "));
	CHECK(!strcmp(after(NULL, "\nfrom public.scat_o"), "scat_only "));
	CHECK(!strcmp(after("q.sql", "."), "k zone qty_left "));
	/* :Connect to a connection with no login yet: none of pg's columns */
	CHECK(!sql_connect(&e, "pg2"));
	CHECK(!strcmp(after(NULL, "."), ""));
	printf("test_snip_sql: the connection's columns after a login (%d loop turns)\n", n);
	core_free(&e);
	return 0;
}
