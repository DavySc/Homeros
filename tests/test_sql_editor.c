/* SQL buffers in the editor (sql.h) against the test servers (see
   test_sql.c): F5 on the whole buffer and on a selection, GO batches and
   GO n, the grids then their messages, Alt+Break, the
   results split opened and closed, the folder's connection and :Connect,
   the connections file's refusals, and the catalog. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32   /* src/sql/check.sh builds it for Windows */
#include <direct.h>
#define mkdir(p, m) _mkdir(p)
#define setenv(k, v, o) _putenv_s(k, v)
#define TMP getenv("TEMP")
/* the colours, which check.sh does not link (tree-sitter), as test_git_win */
int syntax_open(void) { return 0; }
void syntax_edit(void) {}
void syntax_free(void) {}
void syntax_faces(void) {}
int syntax_md(void) { return 0; }
int syntax_conceal(void) { return 0; }
const char *syntax_filetype(const char *p) { (void)p; return 0; }
#else
#define TMP "/tmp"
#endif
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

/* F5 until the session takes it (a cancel answered), at most 3 s: 1 taken */
static int f5_once_idle(void)
{
	for (int i = 0; i < 300; i++) {
		e.msg[0] = 0;
		if (sql_key(&e, KEY_F5) == 1 && !e.msg[0]) return 1;
		loop_once(10);
	}
	return 0;
}


/* table hcat's columns as "schema.hcat.column " (and the header line's,
   which is not a row, and Postgres's own, which are not listed) */
static void hcat(void *ctx, const char *schema, const char *table, const char *column)
{
	char *o = ctx;
	if (!strcmp(table, "hcat") || !strcmp(table, "table_name") || !strcmp(table, "TABLE_NAME") || !strcmp(schema, "pg_catalog")) snprintf(o + strlen(o), 256 - strlen(o), "%s.%s.%s ", schema, table, column);
}

/* the catalog of the current buffer's connection once read (3 s at most): its columns, hcat's in o */
static int catalog(char *o)
{
	int n = 0;
	for (int i = 0; i < 300 && !(n = sql_catalog(&e, e.buf, hcat, (*o = 0, o))); i++) loop_once(10);
	return n;
}

/* path holding sql, opened and run to its end */
static const char *f5(const char *path, const char *sql)
{
	put(path, sql, 0600);
	CHECK(!core_edit_file(&e, path, 0) && sql_key(&e, KEY_F5) == 1);
	return until("Completion time");
}

/* a one-column, one-row set (ASCII, under sql.c's CAP) as the results lay
   it out: the names on their rule, the row, its count */
static const char *one(const char *name, const char *val)
{
	static char o[512];
	size_t w = strlen(name) > strlen(val) ? strlen(name) : strlen(val), n = 0;
	n += (size_t)snprintf(o + n, sizeof o - n, "  \xe2\x94\x82 %-*s\n\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc", (int)w, name);
	for (size_t i = 0; i <= w; i++) n += (size_t)snprintf(o + n, sizeof o - n, "\xe2\x94\x80");
	snprintf(o + n, sizeof o - n, "\n1 \xe2\x94\x82 %s\n(1 row)\n", val);
	return o;
}

/* the Messages rule */
#define MSGS "\xe2\x94\x80\xe2\x94\x80 Messages \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n"

static int nwins(void) { int n = 0; for (Win *w = e.wins; w; w = w->next) n++; return n; }

int main(void)
{
	const char *sa = getenv("SQL_SA_PASSWORD"), *pg = getenv("SQL_PG_PASSWORD");
	if (!sa || !pg) { fprintf(stderr, "test_sql_editor: SQL_SA_PASSWORD and SQL_PG_PASSWORD: start the test servers as src/sql/README.md says\n"); return 1; }
	char dir[512], conf[4096], f[4096];
	snprintf(dir, sizeof dir, "%s/sql_editorXXXXXX", TMP);
	CHECK(mkdtemp(dir) && !chdir(dir) && !mkdir("homeros", 0700) && !mkdir("ms", 0700) && !mkdir("pg", 0700));
	CHECK(!mkdir("ms/deep", 0700) && !mkdir("msx", 0700) && !mkdir("a b", 0700));
	setenv("XDG_CONFIG_HOME", dir, 1);
	setenv("HOME", dir, 1), setenv("USERPROFILE", dir, 1);   /* ~ (Windows: USERPROFILE) */
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	/* no connections file, then one that others can read with a password in it */
	put("ms/a.sql", "select 1 as one\ngo\nprint 'between'\ngo 2\nselect 2 as two\n", 0600);
	CHECK(!core_open(&e, "ms/a.sql"));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "no connections file"));
	/* the longest dir line wins wherever it is; ~ is HOME; a folder with a
	   space; Postgres's port by default */
	snprintf(f, sizeof f, "ms mssql 127.0.0.1 sa master trust pass=%s\npg pg 127.0.0.1 postgres postgres trust pass=%s\n"
		"dir %s/ms/deep pg\ndir ~/ms ms\ndir %s/pg pg\ndir  %s/a b   ms \n"
		"bad mssql 127.0.0.1:99999 sa master pass=x\nletters mssql 127.0.0.1:1433x sa master pass=x\nnopass mssql 127.0.0.1 sa master\nv6 mssql [::1]:1433 sa master pass=x\n"
		"cs mssql 127.0.0.1 sa hcs trust pass=%s\nlim pg 127.0.0.1 hlim postgres trust pass=%s\nnohost mssql nohost.invalid sa master pass=x\nbadpw mssql 127.0.0.1 sa master trust pass=x\n", sa, pg, dir, dir, dir, sa, pg);
	put(conf, f, 0644);
#ifndef _WIN32   /* there the profile is the user's (sql.c, as libpq) */
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "others can read it"));
	CHECK(!chmod(conf, 0600));
#endif
	/* the whole buffer: batches in order, GO 2 twice, the split below */
	CHECK(sql_key(&e, KEY_F5) == 1 && nwins() == 2 && e.win == e.wins);
	/* from the first line: the login's own messages are not shown; each
	   grid's messages after it, as they come */
	char want[1024];
	snprintf(want, sizeof want, "%s\n" MSGS "(1 row affected)\nbetween\nbetween\n\n", one("one", "1"));
	snprintf(want + strlen(want), sizeof want - strlen(want), "%s\n" MSGS "(1 row affected)\n\nCompletion time: ", one("two", "2"));
	CHECK(!strncmp(until("Completion time"), want, strlen(want)));
	/* the status line: SSMS's, its rows, its time, the connection */
	CHECK(e.wins->next && strstr(e.wins->next->buf->status, "\xe2\x9c\x93 Query executed successfully \xc2\xb7 2 rows \xc2\xb7 00:00:0") && strstr(e.wins->next->buf->status, " \xc2\xb7 ms / master"));
	CHECK(e.wins->next && e.wins->next->buf->nomodifiable && e.wins->next->line == text_lines(e.wins->next->buf->text) - 1);   /* the window follows the end */
	/* a selection: only its lines run, on the same connection */
	core_goto(&e, 4, 0);
	keys_visual(&e, 'V', 4);
	CHECK(sql_key(&e, KEY_F5) == 1);
	CHECK(!strncmp(until("Completion time"), one("two", "2"), strlen(one("two", "2"))) && strstr(res, "(1 row affected)\n\nCompletion time"));
	keys_feed(&e, KEY_ESC, 0);
	/* SPC r closes the results and opens them again, the cursor staying in the query */
	CHECK(!sql_results(&e) && nwins() == 1 && !e.buf->nonumber);
	CHECK(!sql_results(&e) && nwins() == 2 && !e.buf->nonumber);
	/* Alt+Break: the run ends, SSMS's line, and the connection runs the next */
	put("ms/b.sql", "select top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", 0600);
	CHECK(!core_edit_file(&e, "ms/b.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 20; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_BREAK) == 1 && strstr(until("cancelled by user"), "Query was cancelled by user.\n"));
	for (int i = 0; i < 300 && !strstr(e.wins->next->buf->status, "canceled"); i++) loop_once(10);
	CHECK(!strncmp(e.wins->next->buf->status, "Query was canceled by user \xc2\xb7 ", 30));
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), one("two", "2")));
	/* a refused GO line: the batches before it run, the text between them
	   and it does not (sqlcmd, measured), the line is named */
	put("ms/c.sql", "select 3 as three\ngo\nselect 4\ngo x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/c.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "line 4"));
	CHECK(strstr(until("Completion time"), one("three", "3")) && !strstr(res, "4\n"));
	/* the folder's connection is Postgres's; :Connect names another */
	put("pg/d.sql", "select 'pg' as x; select 1/0\n", 0600);
	CHECK(!core_edit_file(&e, "pg/d.sql", 0) && sql_key(&e, KEY_F5) == 1);
	CHECK(strstr(until("Completion time"), one("x", "pg")) && strstr(res, "\n(SELECT 1)\nERROR 22012: division by zero\n"));
	CHECK(!sql_connect(&e, "ms") && strstr(e.msg, "ms"));
	put("pg/e.sql", "select @@version v\n", 0600);
	CHECK(!core_edit_file(&e, "pg/e.sql", 0) && !sql_connect(&e, "ms") && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Microsoft SQL Server"));
	CHECK(sql_connect(&e, "nope") && strstr(e.msg, "no connection nope"));
	/* a run sends the text as it was at F5: an edit while batch 1 waits
	   does not reach batch 2 (SqlSess.tla's Snapshot) */
	put("ms/f.sql", "waitfor delay '00:00:00.5'\ngo\nselect 1042 as id\n", 0600);
	CHECK(!core_edit_file(&e, "ms/f.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 10; i++) loop_once(10);
	CHECK(!text_insert(e.buf->text, 40, "  ", 2));   /* "select 104  2 as id" */
	CHECK(strstr(until("Completion time"), one("id", "1042")));
	/* F5 while the server has not answered a cancel says so; the session
	   then runs the next F5 on the same connection (#t still there) */
	put("ms/g.sql", "create table #t (x int)\nselect top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\nselect count(*) n from #t\n", 0600);
	CHECK(!core_edit_file(&e, "ms/g.sql", 0));
	keys_visual(&e, 'V', 0);
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Completion"));
	keys_feed(&e, KEY_ESC, 0);
	core_goto(&e, 1, 0);
	keys_visual(&e, 'V', 1);
	CHECK(sql_key(&e, KEY_F5) == 1);
	keys_feed(&e, KEY_ESC, 0);
	for (int i = 0; i < 20; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_BREAK) == 1 && sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "not answered the cancel"));
	core_goto(&e, 2, 0);
	keys_visual(&e, 'V', 2);
	CHECK(f5_once_idle());
	keys_feed(&e, KEY_ESC, 0);
	CHECK(strstr(until("Completion time"), one("n", "0")));
	/* Alt+Break while logging in drops the connection; F5 then logs in again */
	CHECK(!core_edit_file(&e, "ms/c.sql", 0) && !sql_connect(&e, "ms") && sql_key(&e, KEY_F5) == 1);
	CHECK(sql_key(&e, KEY_BREAK) == 1);
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "three"));
	/* a cancelled run sends none of its later batches */
	put("ms/o.sql", "select top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\ngo\nselect 77 as late\n", 0600);
	CHECK(!core_edit_file(&e, "ms/o.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 20; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "a query is running"));   /* a second F5 waits for the first run */
	/* Ctrl-C cancels as Alt+Break; while that cancel waits, and with no
	   run, it is Vim's */
	CHECK(sql_key(&e, KEY_CTRL_C) == 1 && sql_key(&e, KEY_CTRL_C) == 0);
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!strstr(results(), "late") && strstr(results(), "Query was cancelled by user.\n") && sql_key(&e, KEY_CTRL_C) == 0);
	/* the cancel ends with its answer: the next run sends all its batches */
	CHECK(!text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select 1 as a\ngo\nselect 2 as b\ngo\nselect 3 as c\n", 48));
	CHECK(f5_once_idle() && strstr(until("Completion time"), one("c", "3")));
	/* Postgres: F5 waits for the cancel's own connection to close */
	put("pg/h.sql", "select g from generate_series(1, 20000000) g\n", 0600);
	CHECK(!core_edit_file(&e, "pg/h.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 30; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_BREAK) == 1 && !text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select 7 as seven\n", 18));
	CHECK(f5_once_idle() && strstr(until("Completion time"), one("seven", "7")));
	/* a connection the server ends: its line, and the next F5 logs in again */
	put("pg/i.sql", "select pg_terminate_backend(pg_backend_pid())\n", 0600);
	CHECK(!core_edit_file(&e, "pg/i.sql", 0) && sql_key(&e, KEY_F5) == 1);
	until("terminat");
	for (int i = 0; i < 50; i++) loop_once(10);
	CHECK(!text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select 5 as five\n", 17));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("1 row)"), one("five", "5")));
	/* GO 0 runs its batch no time (sqlcmd) */
	put("ms/j.sql", "select 9 as nine\ngo 0\nselect 8 as eight\n", 0600);
	CHECK(!core_edit_file(&e, "ms/j.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "eight") && !strstr(res, "nine"));
	/* a v selection ends on its last character whole */
	put("ms/k.sql", "select N'caf\xc3\xa9' as x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/k.sql", 0));
	keys_visual(&e, 'v', 0);
	core_goto(&e, 0, 12);
	CHECK(sql_key(&e, KEY_F5) == 1);
	keys_feed(&e, KEY_ESC, 0);
	CHECK(strstr(until("Completion time"), "Unclosed quotation mark after the character string 'caf\xc3\xa9'"));
	/* the folders: the longest dir line, ~, a folder with spaces, /ms not holding /msx */
	put("ms/deep/l.sql", "select version() v\n", 0600);
	CHECK(!core_edit_file(&e, "ms/deep/l.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "PostgreSQL"));
	put("a b/m.sql", "select @@version v\n", 0600);
	CHECK(!core_edit_file(&e, "a b/m.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Microsoft SQL Server"));
	put("msx/n.sql", "select 1\n", 0600);
	CHECK(!core_edit_file(&e, "msx/n.sql", 0) && sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "no connection for this folder"));
	/* a wrong port, no password, an IPv6 address */
	CHECK(sql_connect(&e, "bad") && strstr(e.msg, "port in 127.0.0.1:99999") && sql_connect(&e, "letters") && strstr(e.msg, "port in 127.0.0.1:1433x"));
	CHECK(sql_connect(&e, "nopass") && strstr(e.msg, "no pass="));
	CHECK(!sql_connect(&e, "v6") && strstr(e.msg, "(::1)"));
	/* SPC r from the results: closed, the cursor back in the query */
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	core_win_goto(&e, 'w', 2);
	CHECK(e.buf->nomodifiable && !sql_results(&e) && nwins() == 1 && e.buf->path && strstr(e.buf->path, "a.sql"));
	/* F5 from the results runs their query */
	CHECK(!sql_results(&e) && !core_win_goto(&e, 'w', 2) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), one("two", "2")));
	core_win_goto(&e, 'w', 1);
	/* no room for the split: F5 says so and runs nothing, and the next F5 runs */
	CHECK(!core_only(&e));
	for (int i = 0; i < 30 && !core_split(&e, 0, 0); i++);
	e.msg[0] = 0;
	CHECK(sql_key(&e, KEY_F5) == 1 && e.msg[0] && !strstr(e.msg, "running"));
	CHECK(!core_only(&e) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), one("two", "2")));
	/* :Disconnect ends the session (its temp table goes); F5 logs in again */
	put("ms/p.sql", "create table #d (x int)\n", 0600);
	CHECK(!core_edit_file(&e, "ms/p.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(!sql_disconnect(&e) && strstr(e.msg, "disconnected from ms") && sql_disconnect(&e) && strstr(e.msg, "not connected"));
	CHECK(!text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select * from #d\n", 17));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Invalid object name '#d'"));
	/* F5 on a server that cannot be opened says why */
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && !sql_connect(&e, "nohost") && sql_key(&e, KEY_F5) == 1 && strstr(until("nohost.invalid"), "nohost.invalid"));
	CHECK(!strncmp(e.wins->next->buf->status, "\xe2\x9c\x97 Query completed with errors", 31));
	CHECK(!sql_connect(&e, "badpw") && sql_key(&e, KEY_F5) == 1 && strstr(until("Login failed"), "Login failed"));
	CHECK(!strncmp(e.wins->next->buf->status, "\xe2\x9c\x97 Query completed with errors", 31));
	/* the catalog (completion's): read when a session on the connection
	   logs in, on a connection of its own, closed once read; the columns
	   in their order, names with a tab or a backslash whole; one per connection,
	   kept; NULL reads it again; :Connect drops it; a failed read is tried
	   at the next login; only .sql buffers; never a message */
	char cat[256];
	const char *users = "select count(*) n from sys.dm_exec_sessions where is_user_process = 1\n";
	CHECK(strstr(f5("ms/q.sql", "drop table if exists hcat\ncreate table hcat (k int, b int, [v w] nvarchar(9), [t\tab] int, [a\\b] int)\n"), "Completion"));
	int n = atoi(strstr(f5("ms/v.sql", users), "\n1 \xe2\x94\x82 ") + 6);
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && !sql_catalog(&e, e.buf, NULL, NULL) && catalog(cat) > 0 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b "));
	int same = 0;
	for (int i = 0; i < 20 && !same; i++) same = atoi(strstr(f5("ms/v.sql", users), "\n1 \xe2\x94\x82 ") + 6) == n;
	CHECK(same);
	CHECK(strstr(f5("ms/r.sql", "alter table hcat add z int\n"), "Completion"));
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && !sql_connect(&e, "ms") && sql_catalog(&e, e.buf, hcat, (*cat = 0, cat)) == 0 && !*cat);
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && !sql_catalog(&e, e.buf, hcat, cat));
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time") && catalog(cat) > 0 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b dbo.hcat.z "));
	snprintf(f, sizeof f, "drop table if exists hcat; create table hcat (k int, b int, \"v w\" text, \"t\tab\" int, \"a\\b\" int);"
		" do $$ begin create role hlim login; exception when duplicate_object then null; end $$;"
		" alter role hlim password '%s' connection limit 1; grant select on hcat to hlim\n", pg);
	CHECK(strstr(f5("pg/s.sql", f), "Completion") && !sql_catalog(&e, e.buf, NULL, NULL) && catalog(cat) > 0 && !strcmp(cat, "public.hcat.k public.hcat.b public.hcat.v w public.hcat.t\tab public.hcat.a\\b "));
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && sql_catalog(&e, e.buf, hcat, (*cat = 0, cat)) > 0 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b dbo.hcat.z "));
	put("pg/t.sql", "select 1 as one\n", 0600);
	CHECK(!core_edit_file(&e, "pg/t.sql", 0) && !sql_connect(&e, "lim") && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!sql_catalog(&e, e.buf, hcat, cat));   /* its one connection is the session's */
	CHECK(strstr(f5("pg/u.sql", "alter role hlim connection limit 2\n"), "Completion"));
	CHECK(!core_edit_file(&e, "pg/t.sql", 0) && !sql_disconnect(&e) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(catalog(cat) > 0 && !strcmp(cat, "public.hcat.k public.hcat.b public.hcat.v w public.hcat.t\tab public.hcat.a\\b "));
	CHECK(strstr(f5("ms/w.sql", "if db_id('hcs') is null create database hcs collate Latin1_General_CS_AS\n"), "Completion"));
	put("ms/x.sql", "drop table if exists hcat\ncreate table hcat (K int)\n", 0600);
	CHECK(!core_edit_file(&e, "ms/x.sql", 0) && !sql_connect(&e, "cs") && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(!sql_catalog(&e, e.buf, NULL, NULL) && catalog(cat) > 0 && !strcmp(cat, "dbo.hcat.K "));
	put("ms/y.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/y.txt", 0) && !sql_catalog(&e, e.buf, hcat, cat));
	put("msx/o.sql", "select 1\n", 0600);
	CHECK(!core_edit_file(&e, "msx/o.sql", 0) && strcpy(e.msg, "kept") && !sql_catalog(&e, e.buf, hcat, cat) && !strcmp(e.msg, "kept"));
	/* the grid: a column as wide as its widest value, a longer one cut at
	   40 cells with a ..., a NULL apart from the text 'NULL', a CJK value
	   two cells a character, SSMS's name for a column without one; the faces: the names, the rule, the row
	   numbers, the |s, a NULL, a cut, an error */
	const char *g = f5("ms/z.sql", "select N'NULL' s, null n, replicate(N'x', 50) l, N'\xe6\x97\xa5\xe6\x9c\xac' w, N'\xc3\xa9' e\ngo\nselect 1/0\n");
	CHECK(!strncmp(g, "  \xe2\x94\x82 s    \xe2\x94\x82 n    \xe2\x94\x82 l ", 26) && strstr(g, "\n1 \xe2\x94\x82 NULL \xe2\x94\x82 NULL \xe2\x94\x82 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\xe2\x80\xa6 \xe2\x94\x82 \xe6\x97\xa5\xe6\x9c\xac \xe2\x94\x82 \xc3\xa9\n(1 row)\n") && strstr(g, "\n(1 row)\n\n" MSGS "(1 row affected)\n\n  \xe2\x94\x82 (No column name)\n") && strstr(g, "\n(0 rows)\n"));
	Buf *rb = e.wins->next->buf;
	CHECK(!strncmp(rb->status, "\xe2\x9c\x97 Query completed with errors \xc2\xb7 1 row \xc2\xb7 ", 44));
	CHECK(!sql_disconnect(&e) && !strncmp(rb->status, "\xe2\x9c\x97 Query completed", 18));   /* the line stays between runs */
	unsigned char fc[256];
	size_t l0 = text_line_start(rb->text, 0), l1 = text_line_start(rb->text, 1), l2 = text_line_start(rb->text, 2), l3 = text_line_start(rb->text, 3);
	rb->faces(rb, l0, l1 - 1, fc);
	CHECK(fc[0] == FACE_PMENU_SEL && fc[l1 - 2 - l0] == FACE_PMENU_SEL);
	rb->faces(rb, l1, l2 - 1, fc);
	CHECK(fc[0] == FACE_ORG_TABLE && fc[l2 - 2 - l1] == FACE_ORG_TABLE);
	rb->faces(rb, l2, l3 - 1, fc);
	CHECK(fc[0] == FACE_LINE_NR && fc[2] == FACE_ORG_TABLE && fc[5] == FACE_NONE && fc[11] == FACE_ORG_TABLE && fc[15] == FACE_DIAG_WARN && fc[18] == FACE_DIAG_WARN
		&& fc[19] == FACE_NONE && fc[24] == FACE_NONE && fc[63] == FACE_NUMBER && fc[66] == FACE_NONE && fc[67] == FACE_ORG_TABLE);
	rb->faces(rb, l2 + 16, l2 + 18, fc);   /* a window that starts inside a span */
	CHECK(fc[0] == FACE_DIAG_WARN && fc[1] == FACE_DIAG_WARN);
	const char *m = strstr(g, "Msg 8134");
	size_t at = text_len(rb->text) - strlen(g) + (size_t)(m - g);
	rb->faces(rb, at, at + 8, fc);
	CHECK(m && fc[0] == FACE_DIAG_ERROR && fc[7] == FACE_DIAG_ERROR);
	m = strstr(g, "Divide by zero");   /* its second line */
	at = text_len(rb->text) - strlen(g) + (size_t)(m - g);
	rb->faces(rb, at, at + 6, fc);
	CHECK(m && fc[0] == FACE_DIAG_ERROR && fc[5] == FACE_DIAG_ERROR);
	/* the row count, the Messages rule, a later grid's names */
	m = strstr(g, "(1 row)\n\n" MSGS);
	at = text_len(rb->text) - strlen(g) + (size_t)(m - g);
	rb->faces(rb, at, at + 10, fc);
	CHECK(fc[0] == FACE_ORG_TABLE && fc[6] == FACE_ORG_TABLE && fc[7] == FACE_NONE && fc[9] == FACE_FUNCTION);
	m = strstr(g, "(No column name)");
	at = text_len(rb->text) - strlen(g) + (size_t)(m - g);
	rb->faces(rb, at, at + 2, fc);
	CHECK(fc[0] == FACE_PMENU_SEL);
	/* Enter on a cell: its whole value in a split (a cut one, a NULL);
	   not on a row number or the names */
	size_t cx = (size_t)(strstr(g, "xxx") - strstr(g, "\n1 ")) - 1;
	CHECK(!core_win_goto(&e, 'j', 1) && e.buf == rb);
	core_goto(&e, 2, cx);
	CHECK(sql_key(&e, KEY_ENTER) == 1 && e.buf != rb && text_len(e.buf->text) == 50 && !core_close(&e, 1) && e.buf == rb);
	core_goto(&e, 2, 12);
	CHECK(sql_key(&e, KEY_ENTER) == 1 && text_len(e.buf->text) == 4 && !core_close(&e, 1));
	core_goto(&e, 2, 0);
	CHECK(sql_key(&e, KEY_ENTER) == 0);
	core_goto(&e, 0, 5);
	CHECK(sql_key(&e, KEY_ENTER) == 0 && !core_win_goto(&e, 'k', 1));
	/* a wide character two cells, a control byte two (^A), in the width;
	   Enter gives a tab back whole */
	g = f5("ms/wide.sql", "select N'\xe6\x97\xa5\xe6\x9c\xac' w, char(1) c, N'a' + char(9) + N'b' t union all select N'ab', 'a', 'x'\n");
	CHECK(strstr(g, "\n2 \xe2\x94\x82 ab   \xe2\x94\x82 a  \xe2\x94\x82 x\n"));
	CHECK(!core_win_goto(&e, 'j', 1));
	core_goto(&e, 2, 24);
	CHECK(sql_key(&e, KEY_ENTER) == 1 && text_read(e.buf->text, 0, cat, sizeof cat) == 3 && !memcmp(cat, "a\tb", 3) && !core_close(&e, 1) && !core_win_goto(&e, 'k', 1));
	core_goto(&e, 0, 0);
	CHECK(sql_key(&e, KEY_ENTER) == 0);   /* the query's own */
	/* Postgres's notices between a set's rows wait for its end; its
	   error makes the status line SSMS's error one */
	g = f5("pg/n.sql", "create function pg_temp.n(i int) returns int as $$ begin raise notice 'row %', i; return i; end $$ language plpgsql;\n"
		"select pg_temp.n(g) as v from generate_series(1, 3) g\n");
	CHECK(strstr(g, "\n3 \xe2\x94\x82 3\n(3 rows)\n\n" MSGS "NOTICE 00000: row 1\nNOTICE 00000: row 2\nNOTICE 00000: row 3\n(SELECT 3)\n"));
	CHECK(strstr(f5("pg/err.sql", "select nope\n"), "nope") && !strncmp(e.wins->next->buf->status, "\xe2\x9c\x97 Query completed with errors", 31));
	/* a message comes at once (RAISERROR WITH NOWAIT) while the batch
	   runs, after a set's end; the status line while it runs; a set's
	   rows show while it waits (a row past Postgres's 8 KB send buffer
	   goes at once); Enter is Vim's in insert mode or a half typed command */
	put("ms/nowait.sql", "select 1 x; raiserror('early', 0, 1) with nowait; waitfor delay '00:00:02'\nselect 1 x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/nowait.sql", 0) && sql_key(&e, KEY_F5) == 1);
	CHECK(!strncmp(e.wins->next->buf->status, "Executing query... \xc2\xb7 ms / master", 26));
	CHECK(strstr(until("early"), "(1 row)\n\n" MSGS "(1 row affected)\nearly\n") && !strstr(res, "Completion"));
	until("Completion time");
	put("pg/trickle.sql", "select g, pg_sleep(g - 1), repeat('x', 9000) from generate_series(1, 3) g\n", 0600);
	CHECK(!core_edit_file(&e, "pg/trickle.sql", 0) && sql_key(&e, KEY_F5) == 1);
	CHECK(strstr(until("\n      1 \xe2\x94\x82 1 "), "\xe2\x94\x82 g ") && !strstr(res, "Completion"));
	CHECK(!core_win_goto(&e, 'j', 1) && (core_goto(&e, 2, 10), e.mode = MODE_INSERT, sql_key(&e, KEY_ENTER) == 0) && (e.mode = MODE_NORMAL, e.ks.pending = 1, sql_key(&e, KEY_ENTER) == 0));
	e.ks.pending = 0;
	CHECK(!core_win_goto(&e, 'k', 1));
	CHECK(strstr(until("Completion time"), "\n      3 \xe2\x94\x82 3 ") && strstr(res, "\n(3 rows)\n"));
	/* a connection that dies inside a set, long after its rows began to
	   show: the set ends, its messages and the reason after it */
	put("pg/die.sql", "select g, pg_sleep(0.3 * g), case when g = 2 then pg_terminate_backend(pg_backend_pid()) end, repeat('x', 9000) from generate_series(1, 2) g\n", 0600);
	CHECK(!core_edit_file(&e, "pg/die.sql", 0) && sql_key(&e, KEY_F5) == 1);
	CHECK(strstr(until("57P01"), "\n(2 rows)\n\n" MSGS "FATAL 57P01: terminating connection"));
	/* a long set: the widths from its first 500 rows, a later longer value
	   cut, the row numbers wide enough for 7 digits */
	g = f5("ms/long.sql", "select top 600 case when row_number() over (order by (select 1)) > 500 then N'longer' else N'v' end x from sys.all_objects a, sys.all_objects b\n");
	CHECK(strstr(g, "\n    500 \xe2\x94\x82 v\n    501 \xe2\x94\x82 \xe2\x80\xa6\n") && strstr(g, "\n    600 \xe2\x94\x82 \xe2\x80\xa6\n(600 rows)\n"));
	/* not a SQL buffer: F5 is not ours */
	put("x.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "x.txt", 0) && !sql_key(&e, KEY_F5));
	core_free(&e);
#ifdef _WIN32   /* winpass.sh's summary line */
	fprintf(stderr, "test_sql_editor: F5, GO, a selection, Alt+Break, the results split, :Connect\n");
#endif
	return 0;
}
