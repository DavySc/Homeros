/* SQL buffers in the editor (sql.h) against the test servers (see
   test_sql.c): F5 on the whole buffer and on a selection, GO batches and
   GO n, messages next to rows in the server's order, Alt+Break, the
   results split opened and closed, the folder's connection and :Connect,
   and the connections file's refusals. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "sql.h"
#include "text.h"

static Editor e;
static char res[1 << 16];

/* the buffer shown below the query, as text: its last 64 KB */
static const char *results(void)
{
	Win *w = e.wins;
	while (w && !w->buf->nonumber) w = w->next;
	size_t z = w ? text_len(w->buf->text) : 0, a = z > sizeof res - 1 ? z - (sizeof res - 1) : 0;
	size_t n = w ? text_read(w->buf->text, a, res, z - a) : 0;
	res[n] = 0;
	return res;
}

/* runs the loop until the results hold s */
static const char *until(const char *s)
{
	for (int i = 0; i < 3000 && !strstr(results(), s); i++) loop_once(10);
	if (!strstr(res, s)) fprintf(stderr, "results:\n%s\nwant: %s\n", res, s);
	return res;
}

static void put(const char *path, const char *s, int mode)
{
	FILE *f = fopen(path, "w");
	CHECK(f && fputs(s, f) >= 0 && !fclose(f) && !chmod(path, (mode_t)mode));
}

static int nwins(void) { int n = 0; for (Win *w = e.wins; w; w = w->next) n++; return n; }

int main(void)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD");
	if (!sa || !pg) { fprintf(stderr, "test_sql_editor: SQL_SA_PASSWORD and SQL_PG_PASSWORD: start the test servers as src/sql/README.md says\n"); return 1; }
	char dir[] = "/tmp/sql_editorXXXXXX", conf[4096], f[4096];
	CHECK(mkdtemp(dir) && !chdir(dir) && !mkdir("homeros", 0700) && !mkdir("ms", 0700) && !mkdir("pg", 0700));
	setenv("XDG_CONFIG_HOME", dir, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	/* no connections file, then one that others can read with a password in it */
	put("ms/a.sql", "select 1 as one\ngo\nprint 'between'\ngo 2\nselect 2 as two\n", 0600);
	CHECK(!core_open(&e, "ms/a.sql"));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "no connections file"));
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1:5432 postgres postgres trust pass=%s\ndir %s/ms ms\ndir %s/pg pg\n", sa, pg, dir, dir);
	put(conf, f, 0644);
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "others can read it"));
	CHECK(!chmod(conf, 0600));
	/* the whole buffer: batches in order, GO 2 twice, the split below */
	CHECK(sql_key(&e, KEY_F5) == 1 && nwins() == 2 && e.win == e.wins);
	CHECK(strstr(until("Completion time"), "one\n1\n(1 row affected)\nbetween\nbetween\ntwo\n2\n(1 row affected)\n\nCompletion time: "));
	/* a selection: only its lines run, on the same connection */
	core_goto(&e, 4, 0);
	keys_visual(&e, 'V', 4);
	CHECK(sql_key(&e, KEY_F5) == 1);
	CHECK(!strncmp(until("Completion time"), "two\n2\n(1 row affected)\n\nCompletion time", 37));
	keys_feed(&e, KEY_ESC, 0);
	/* SPC r closes the results and opens them again, the cursor staying in the query */
	CHECK(!sql_results(&e) && nwins() == 1 && !e.buf->nonumber);
	CHECK(!sql_results(&e) && nwins() == 2 && !e.buf->nonumber);
	/* Alt+Break: the run ends, SSMS's line, and the connection runs the next */
	put("ms/b.sql", "select top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", 0600);
	CHECK(!core_edit_file(&e, "ms/b.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 20; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_BREAK) == 1 && strstr(until("cancelled by user"), "Query was cancelled by user.\n"));
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "two\n2\n"));
	/* a refused GO line: the batches before it run, the text between them
	   and it does not (sqlcmd, measured), the line is named */
	put("ms/c.sql", "select 3 as three\ngo\nselect 4\ngo x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/c.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "line 4"));
	CHECK(strstr(until("Completion time"), "three\n3\n") && !strstr(res, "4\n"));
	/* the folder's connection is Postgres's; :Connect names another */
	put("pg/d.sql", "select 'pg' as x; select 1/0\n", 0600);
	CHECK(!core_edit_file(&e, "pg/d.sql", 0) && sql_key(&e, KEY_F5) == 1);
	CHECK(strstr(until("Completion time"), "x\npg\n(SELECT 1)\nERROR 22012: division by zero\n"));
	CHECK(!sql_connect(&e, "ms") && strstr(e.msg, "ms"));
	put("pg/e.sql", "select @@version v\n", 0600);
	CHECK(!core_edit_file(&e, "pg/e.sql", 0) && !sql_connect(&e, "ms") && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Microsoft SQL Server"));
	CHECK(sql_connect(&e, "nope") && strstr(e.msg, "no connection nope"));
	/* not a SQL buffer: F5 is not ours */
	put("x.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "x.txt", 0) && !sql_key(&e, KEY_F5));
	core_free(&e);
	return 0;
}
