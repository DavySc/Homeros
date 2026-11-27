/* PR 175 review probe: the editor's side of "Ask on F5".
   p175 SCENARIO, with SQL_SA_PASSWORD and SQL_PG_PASSWORD set and the
   test servers up.  Keys go through keys_input, as main.c sends them.
   Prints what the command line holds and what the results show. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "sql.h"
#include "text.h"

static Editor e;
static char res[1 << 16];

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
static void cmd(const char *what)
{
	printf("%s: mode %s", what, e.mode == MODE_CMDLINE ? "CMDLINE" : e.mode == MODE_NORMAL ? "NORMAL" : "other");
	if (e.mode == MODE_CMDLINE) printf(" cmd(%d) [:%.*s]", (int)e.cmdlen, (int)e.cmdlen, e.cmd);
	printf(" msg [%s]\n", e.msg);
}
static void sel(long l1, long n) { if (e.ks.visual) keys_input(&e, KEY_ESC, 0); core_goto(&e, l1 - 1, 0); keys_visual(&e, 0, 0); core_goto(&e, l1 - 1, 0); keys_visual(&e, (char)86, (size_t)(l1 + n - 2)); }
static void open(const char *p, const char *s) { put(p, s); if (core_edit_file(&e, p, 0)) printf("open failed\n"); }
static void show(const char *what) { printf("--- %s\n%s---\n", what, until("Completion time")); }
static void wait_ms(int ms) { for (int i = 0; i < ms / 10; i++) loop_once(10); }

int main(int argc, char **argv)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD"), *sc = argc > 1 ? argv[1] : "";
	char dir[512] = "/tmp/p175XXXXXX", conf[4096], f[4096];
	if (!sa || !pg) return puts("passwords"), 1;
	mkdtemp(dir), chdir(dir), mkdir("homeros", 0700), mkdir("ms", 0700), mkdir("pg", 0700);
	setenv("XDG_CONFIG_HOME", dir, 1), setenv("HOME", dir, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1 postgres postgres trust pass=%s\ndir ~/ms ms\ndir ~/pg pg\n", sa, pg);
	put(conf, f);
	put("ms/first.sql", "select 1\n");
	core_open(&e, "ms/first.sql");

	if (!strcmp(sc, "davy")) {   /* Davy's ask: the DECLARE above, a SELECT that feeds an INSERT selected */
		open("ms/d.sql", "declare @a int = 3\ncreate table #t (x int)\ngo\ninsert into #t\nselect x from (values (1), (3), (3)) v(x) where x = @a\ngo\nselect count(*) as n from #t\n");
		sel(2, 2), keys_input(&e, KEY_F5, 0), show("create");
		sel(4, 2), keys_input(&e, KEY_F5, 0), cmd("after F5 on the insert");
		keys("\r"), show("after Enter");
		sel(4, 2), keys_input(&e, KEY_F5, 0), cmd("second F5"), show("second run");
		sel(7, 1), keys_input(&e, KEY_F5, 0), show("count");
	} else if (!strcmp(sc, "esc")) {   /* Esc on the prompt, edits, then :Declare by hand */
		open("ms/e.sql", "create table #t (x int)\ninsert #t values (1), (2), (3)\ngo\ndelete from #t where x = @a\ngo\nselect count(*) as n from #t\n");
		sel(1, 2), keys_input(&e, KEY_F5, 0), show("setup");
		sel(4, 1), keys_input(&e, KEY_F5, 0), cmd("F5 on the delete");
		keys("\033"), cmd("after Esc");
		/* the WHERE moved to its own line under a comment line: the same byte range is now other text */
		core_goto(&e, 3, 0);
		keys("0fwhxi\r-- only the picked ones\r\033");
		{ char t[400]; size_t n = text_read(e.buf->text, 0, t, 399); t[n] = 0; printf("buffer now:\n%s", t); }
		if (argc > 2) wait_ms(atoi(argv[2]));
		keys(":Declare @a int = 2\r"), cmd("after :Declare by hand");
		show("what ran");
		sel(7, 1), keys_input(&e, KEY_F5, 0), show("count");
	} else if (!strcmp(sc, "utf8")) {
		open("ms/u.sql", "declare @n nvarchar(20) = N'caf\xc3\xa9 \xe2\x82\xac'\nselect @n as n, len(@n) as l\n");
		sel(2, 1), keys_input(&e, KEY_F5, 0), cmd("F5");
		keys("\r"), show("run");
	} else if (!strcmp(sc, "long")) {
		char s[2000] = "declare @t table (", *p;
		for (int i = 0; i < 40; i++) p = s + strlen(s), sprintf(p, "column_%02d int%s", i, i < 39 ? ", " : ")\n");
		strcat(s, "select count(*) as n from @t\n");
		open("ms/l.sql", s);
		sel(2, 1), keys_input(&e, KEY_F5, 0), cmd("F5");
		keys("\r"), cmd("after Enter"), show("run");
	} else if (!strcmp(sc, "comment")) {
		open("ms/c.sql", "declare @a int -- the id\n\t= 4\nselect @a as a\n");
		sel(3, 1), keys_input(&e, KEY_F5, 0), cmd("F5");
		keys("\r"), cmd("after Enter"), wait_ms(1500), printf("--- results\n%s---\n", results());
	} else if (!strcmp(sc, "stale")) {   /* the file's DECLARE changes after a value is kept */
		open("ms/s.sql", "declare @a int = 3\nselect @a as a\n");
		sel(2, 1), keys_input(&e, KEY_F5, 0), keys("\r"), show("first");
		core_goto(&e, 0, 0), keys("$r7");
		{ char t[100]; size_t n = text_read(e.buf->text, 0, t, 99); t[n] = 0; printf("buffer now:\n%s", t); }
		sel(2, 1), keys_input(&e, KEY_F5, 0), cmd("F5 again"), show("second");
		keys_input(&e, KEY_F5, 0), show("whole file");
	} else if (!strcmp(sc, "lines")) {   /* error line numbers with kept values, GO batches, GO n */
		open("ms/n.sql", "select 1 as one\ngo\n\nselect @a as a\nselect 1/0\ngo 2\nselect @b as b,\n  1/0\n");
		keys(":Declare @a int = 5\r:Declare @b int = 6\r");
		keys_input(&e, KEY_F5, 0), show("whole file");
		sel(4, 2), keys_input(&e, KEY_F5, 0), show("lines 4-5 selected");
		open("ms/n2.sql", "select 1 as one\ngo\n\nselect 5 as a\nselect 1/0\ngo 2\nselect 6 as b,\n  1/0\n");
		keys_input(&e, KEY_F5, 0), show("the same with constants");
	} else if (!strcmp(sc, "two")) {
		open("ms/t.sql", "declare @a int = 1, @b varchar(9) = 'x'\ndeclare @t table (x int)\nselect @a as a, @b as b, @c as c from @t\n");
		sel(3, 1), keys_input(&e, KEY_F5, 0), cmd("F5");
		keys("\r"), cmd("Enter 1"); keys("\r"), cmd("Enter 2"); keys("\r"), cmd("Enter on the empty prompt");
		keys_input(&e, KEY_F5, 0), cmd("F5 again"); keys("int = 9\r"), cmd("Enter 3"); keys("\r"), cmd("Enter 4");
		show("run"); keys(":Declare\r"), cmd("list");
	} else if (!strcmp(sc, "case")) {
		open("ms/k.sql", "declare @x int = case when 1 = 1 then 1 else 2 end, @y int = 5\nselect @x as x, @y as y\n");
		keys_input(&e, KEY_F5, 0), cmd("F5 on the whole file");
		keys("int = 7\r"), cmd("Enter"), show("run");
		open("ms/k2.sql", "declare @x int = case when 1 = 1 then 1 else 2 end\ngo\nselect @x as x\n");
		sel(3, 1), keys_input(&e, KEY_F5, 0), cmd("F5 on line 3");
		keys("\r"), show("run with the default");
	} else if (!strcmp(sc, "results")) {   /* F5 in the results window; insert mode */
		open("ms/r.sql", "select @a as a\n");
		keys(":Declare @a int = 1\r"), keys_input(&e, KEY_F5, 0), show("first");
		keys(":Declare @a\r");
		core_win_goto(&e, 'j', 1); printf("in results: %d\n", e.buf->nonumber);
		keys_input(&e, KEY_F5, 0), cmd("F5 in the results");
		keys("int = 2\r"), cmd("Enter"), show("run");
		core_win_goto(&e, 'k', 1); keys(":Declare @a\r"); keys("i"), keys_input(&e, KEY_F5, 0), cmd("F5 in insert mode");
		keys("int = 3\r"), cmd("Enter"), show("run");
	} else if (!strcmp(sc, "persist")) {   /* values per file, across :Disconnect, :Connect and another buffer */
		open("ms/p1.sql", "select @a as a\n");
		keys(":Declare @a int = 1\r"), keys_input(&e, KEY_F5, 0), show("p1");
		open("ms/p2.sql", "select @a as a2\n");
		keys_input(&e, KEY_F5, 0), cmd("F5 in p2"); keys("int = 2\r"), show("p2");
		keys(":Disconnect\r"), keys_input(&e, KEY_F5, 0), cmd("F5 after :Disconnect"), show("p2 again");
		keys(":Connect ms\r"), keys_input(&e, KEY_F5, 0), cmd("F5 after :Connect"), show("p2 again");
		core_edit_file(&e, "ms/p1.sql", 0), keys_input(&e, KEY_F5, 0), cmd("F5 back in p1"), show("p1 again");
		keys(":w\r:e\r"), keys_input(&e, KEY_F5, 0), cmd("F5 after :e"), show("p1 after :e");
	} else if (!strcmp(sc, "pg")) {
		open("pg/q.sql", "select @n as a, '@x' as b from (select -3 as n) t\n");
		keys_input(&e, KEY_F5, 0), cmd("F5"), show("run");
		keys(":Declare @n int = 1\r"), cmd(":Declare in a Postgres buffer"), keys_input(&e, KEY_F5, 0), show("run");
	} else if (!strcmp(sc, "misc")) {
		open("ms/m.sql", "select @@rowcount as r, '@s' as s, 1 as [@n] -- @c\n/* @d */ select @@version as v into #v\n"
			"declare @t table (x int)\ninsert @t values (1)\nselect * from @t\n");
		keys_input(&e, KEY_F5, 0), cmd("F5 whole file"), show("run");
		sel(4, 2), keys_input(&e, KEY_F5, 0), cmd("F5 on the table variable's use");
		keys("\r"), cmd("Enter"), show("run");
	} else if (!strcmp(sc, "busy")) {
		open("ms/b.sql", "waitfor delay '00:00:02'\nselect @a as a\n");
		sel(1, 1), keys_input(&e, KEY_F5, 0), wait_ms(300);
		sel(2, 1), keys_input(&e, KEY_F5, 0), cmd("F5 while running"), show("first");
		sel(2, 1), keys_input(&e, KEY_F5, 0), cmd("F5"); 
		/* answer while another run is busy */
		keys("\033"), sel(1, 1), keys_input(&e, KEY_F5, 0), wait_ms(300), keys(":Declare @a int = 4\r"), cmd(":Declare while a run is busy"), show("run"), wait_ms(2500), printf("%s", results());
	} else if (!strcmp(sc, "n2")) {
		open("ms/n2.sql", "select 1 as one\ngo\n\nselect 5 as a\nselect 1/0\ngo 2\nselect 6 as b,\n  1/0\n");
		keys_input(&e, KEY_F5, 0), show("constants"); wait_ms(1000); printf("later:\n%s", results());
		open("ms/n3.sql", "declare @a int = 5\nselect @a as a\n");
		keys_input(&e, KEY_F5, 0), show("a file's own DECLARE with a value");
	} else return puts("scenario?"), 1;
	return 0;
}
