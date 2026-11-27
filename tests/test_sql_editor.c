/* SQL buffers in the editor (sql.h) against the test servers (see
   test_sql.c): F5 on the whole buffer and on a selection, GO batches and
   GO n, messages next to rows in the server's order, Alt+Break, the
   results split opened and closed, the folder's connection and :Connect,
   the connections file's refusals, and the catalog. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

static void typed(const char *s)   /* keys as main.c gives them */
{
	for (; *s; s++) keys_input(&e, (unsigned char)*s, 0);
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


/* hcat's columns as "schema.hcat.column ", or with column NULL the table
   as "schema.hcat " (and the header line's, which is not a row, and
   Postgres's own tables, which are not listed) */
static void hcat(void *ctx, const char *schema, const char *table, const char *column)
{
	char *o = ctx;
	if (!strcasecmp(table, "hcat") || !strcasecmp(table, "table_name") || !strcmp(schema, "pg_catalog"))
		snprintf(o + strlen(o), 256 - strlen(o), column ? "%s.%s.%s " : "%s.%s ", schema, table, column);
}

/* the catalog of the current buffer's connection once read (3 s at most): its columns, hcat's in o */
static int catalog(char *o)
{
	int n = 0;
	for (int i = 0; i < 300 && !(n = sql_catalog(&e, e.buf, "hcat", hcat, (*o = 0, o))); i++) loop_once(10);
	return n;
}

/* path holding sql, opened and run to its end */
static const char *f5(const char *path, const char *sql)
{
	put(path, sql, 0600);
	CHECK(!core_edit_file(&e, path, 0) && sql_key(&e, KEY_F5) == 1);
	return until("Completion time");
}

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
		"cs mssql 127.0.0.1 sa hcs trust pass=%s\nlim pg 127.0.0.1 hlim postgres trust pass=%s\nnohost mssql nohost.invalid sa master pass=x\n"
		"sp mssql 127.0.0.1 hsp master trust pass=Ho meros 1 x\r\nbypass=x mssql 127.0.0.1 hpw master trust\tpass=Ho pass=1  \n"
		"notrust mssql 127.0.0.1 sa master pass=%s\n", sa, pg, dir, dir, dir, sa, pg, sa);
	put(conf, f, 0644);
#ifndef _WIN32   /* there the profile is the user's (sql.c, as libpq) */
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(e.msg, "others can read it"));
	CHECK(!chmod(conf, 0600));
#endif
	/* the whole buffer: batches in order, GO 2 twice, the split below */
	CHECK(sql_key(&e, KEY_F5) == 1 && nwins() == 2 && e.win == e.wins);
	/* from the first line: the login's own messages are not shown */
	CHECK(!strncmp(until("Completion time"), "one\n1\n(1 row affected)\nbetween\nbetween\ntwo\n2\n(1 row affected)\n\nCompletion time: ", 75));
	CHECK(e.wins->next && e.wins->next->buf->nomodifiable && e.wins->next->line == text_lines(e.wins->next->buf->text) - 1);   /* the window follows the end */
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
	/* a run sends the text as it was at F5: an edit while batch 1 waits
	   does not reach batch 2 (SqlSess.tla's Snapshot) */
	put("ms/f.sql", "waitfor delay '00:00:00.5'\ngo\nselect 1042 as id\n", 0600);
	CHECK(!core_edit_file(&e, "ms/f.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 10; i++) loop_once(10);
	CHECK(!text_insert(e.buf->text, 40, "  ", 2));   /* "select 104  2 as id" */
	CHECK(strstr(until("Completion time"), "id\n1042\n"));
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
	CHECK(strstr(until("Completion time"), "n\n0\n"));
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
	CHECK(f5_once_idle() && strstr(until("Completion time"), "c\n3\n"));
	/* Postgres: F5 waits for the cancel's own connection to close */
	put("pg/h.sql", "select g from generate_series(1, 20000000) g\n", 0600);
	CHECK(!core_edit_file(&e, "pg/h.sql", 0) && sql_key(&e, KEY_F5) == 1);
	for (int i = 0; i < 30; i++) loop_once(10);
	CHECK(sql_key(&e, KEY_BREAK) == 1 && !text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select 7 as seven\n", 18));
	CHECK(f5_once_idle() && strstr(until("Completion time"), "seven\n7\n"));
	/* a connection the server ends: its line, and the next F5 logs in again */
	put("pg/i.sql", "select pg_terminate_backend(pg_backend_pid())\n", 0600);
	CHECK(!core_edit_file(&e, "pg/i.sql", 0) && sql_key(&e, KEY_F5) == 1);
	until("terminat");
	for (int i = 0; i < 50; i++) loop_once(10);
	CHECK(!text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select 5 as five\n", 17));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("five"), "five\n5\n"));
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
	/* pass= takes the rest of the line: a password with spaces, on a CRLF line */
	CHECK(strstr(f5("ms/sp.sql", "if suser_id('hsp') is null create login hsp with password = 'Ho meros 1 x', check_policy = off\n"), "Completion"));
	put("ms/sq.sql", "select suser_name() as who\n", 0600);
	CHECK(!core_edit_file(&e, "ms/sq.sql", 0) && !sql_connect(&e, "sp") && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "who\nhsp\n"));
	/* a name holding pass=, a tab before pass=, the first pass= wins, trailing spaces kept */
	CHECK(strstr(f5("ms/sp2.sql", "if suser_id('hpw') is null create login hpw with password = 'Ho pass=1  ', check_policy = off\n"), "Completion"));
	CHECK(!core_edit_file(&e, "ms/sq.sql", 0) && !sql_connect(&e, "bypass=x") && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "who\nhpw\n"));
	/* without trust the server's own certificate is refused */
	CHECK(!core_edit_file(&e, "ms/sq.sql", 0) && !sql_connect(&e, "notrust") && sql_key(&e, KEY_F5) == 1 && strstr(until("verify failed"), "certificate verify failed"));
	/* SPC r from the results: closed, the cursor back in the query */
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	core_win_goto(&e, 'w', 2);
	CHECK(e.buf->nomodifiable && !sql_results(&e) && nwins() == 1 && e.buf->path && strstr(e.buf->path, "a.sql"));
	/* F5 from the results runs their query */
	CHECK(!sql_results(&e) && !core_win_goto(&e, 'w', 2) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "two\n2"));
	core_win_goto(&e, 'w', 1);
	/* no room for the split: F5 says so and runs nothing, and the next F5 runs */
	CHECK(!core_only(&e));
	for (int i = 0; i < 30 && !core_split(&e, 0, 0); i++);
	e.msg[0] = 0;
	CHECK(sql_key(&e, KEY_F5) == 1 && e.msg[0] && !strstr(e.msg, "running"));
	CHECK(!core_only(&e) && sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "two\n2"));
	/* :Disconnect ends the session (its temp table goes); F5 logs in again */
	put("ms/p.sql", "create table #d (x int)\n", 0600);
	CHECK(!core_edit_file(&e, "ms/p.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(!sql_disconnect(&e) && strstr(e.msg, "disconnected from ms") && sql_disconnect(&e) && strstr(e.msg, "not connected"));
	CHECK(!text_delete(e.buf->text, 0, text_len(e.buf->text)) && !text_insert(e.buf->text, 0, "select * from #d\n", 17));
	CHECK(sql_key(&e, KEY_F5) == 1 && strstr(until("Completion time"), "Invalid object name '#d'"));
	/* F5 on a server that cannot be opened says why */
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && !sql_connect(&e, "nohost") && sql_key(&e, KEY_F5) == 1 && strstr(until("nohost.invalid"), "nohost.invalid"));
	/* the catalog (completion's): read when a session on the connection
	   logs in, on a connection of its own, closed once read; the columns
	   in their order, names with a tab or a backslash whole; one per connection,
	   kept; NULL reads it again; :Connect drops it; a failed read is tried
	   at the next login; only .sql buffers; never a message */
	char cat[256];
	const char *users = "select count(*) n from sys.dm_exec_sessions where is_user_process = 1\n";
	CHECK(strstr(f5("ms/q.sql", "drop table if exists hca\ncreate table hca (y int)\ndrop table if exists hcb\ncreate table hcb (y int)\ndrop table if exists hcat\ncreate table hcat (k int, b int, [v w] nvarchar(9), [t\tab] int, [a\\b] int)\n"), "Completion"));
	int n = atoi(strstr(f5("ms/v.sql", users), "n\n") + 2);
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && !sql_catalog(&e, e.buf, NULL, NULL, NULL) && catalog(cat) == 5 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b "));
	/* the count is the table's alone (hca before it, hcb after it), a whole name, never empty */
	CHECK(sql_catalog(&e, e.buf, "hca", hcat, cat) == 1 && sql_catalog(&e, e.buf, "hcb", hcat, cat) == 1 && !sql_catalog(&e, e.buf, "", hcat, cat) && !sql_catalog(&e, e.buf, "hc", hcat, cat));
	CHECK(sql_catalog(&e, e.buf, NULL, hcat, (*cat = 0, cat)) > 0 && !strcmp(cat, "dbo.hcat "));   /* the tables, once each */
	int same = 0;
	for (int i = 0; i < 20 && !same; i++) same = atoi(strstr(f5("ms/v.sql", users), "n\n") + 2) == n;
	CHECK(same);
	CHECK(strstr(f5("ms/r.sql", "alter table hcat add z int\n"), "Completion"));
	for (int i = 0; i < 100; i++) loop_once(10);   /* r.sql's login read nothing: once per connection */
	CHECK(catalog(cat) > 0 && !strstr(cat, ".z "));
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && !sql_connect(&e, "ms") && sql_catalog(&e, e.buf, "hcat", hcat, (*cat = 0, cat)) == 0 && !*cat);
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && !sql_catalog(&e, e.buf, "hcat", hcat, cat));
	CHECK(!core_edit_file(&e, "ms/a.sql", 0) && sql_key(&e, KEY_F5) == 1 && until("Completion time") && catalog(cat) > 0 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b dbo.hcat.z "));
	snprintf(f, sizeof f, "drop table if exists hcat; create table hcat (k int, b int, \"v w\" text, \"t\tab\" int, \"a\\b\" int);"
		" do $$ begin create role hlim login; exception when duplicate_object then null; end $$;"
		" alter role hlim password '%s' connection limit 1; grant select on hcat to hlim\n", pg);
	CHECK(strstr(f5("pg/s.sql", f), "Completion") && !sql_catalog(&e, e.buf, NULL, NULL, NULL) && catalog(cat) > 0 && !strcmp(cat, "public.hcat.k public.hcat.b public.hcat.v w public.hcat.t\tab public.hcat.a\\b "));
	CHECK(sql_catalog(&e, e.buf, NULL, hcat, (*cat = 0, cat)) > 0 && !strcmp(cat, "public.hcat "));
	CHECK(!core_edit_file(&e, "ms/q.sql", 0) && sql_catalog(&e, e.buf, "hcat", hcat, (*cat = 0, cat)) > 0 && !strcmp(cat, "dbo.hcat.k dbo.hcat.b dbo.hcat.v w dbo.hcat.t\tab dbo.hcat.a\\b dbo.hcat.z "));
	put("pg/t.sql", "select 1 as one\n", 0600);
	CHECK(!core_edit_file(&e, "pg/t.sql", 0) && !sql_connect(&e, "lim") && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!sql_catalog(&e, e.buf, "hcat", hcat, cat));   /* its one connection is the session's */
	CHECK(strstr(f5("pg/u.sql", "alter role hlim connection limit 2\n"), "Completion"));
	CHECK(!core_edit_file(&e, "pg/t.sql", 0) && !sql_disconnect(&e) && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(catalog(cat) > 0 && !strcmp(cat, "public.hcat.k public.hcat.b public.hcat.v w public.hcat.t\tab public.hcat.a\\b "));
	CHECK(strstr(f5("ms/w.sql", "if db_id('hcs') is null create database hcs collate Latin1_General_CS_AS\n"), "Completion"));
	CHECK(strstr(f5("ms/x.sql", "drop table if exists hcs.dbo.hcat\ncreate table hcs.dbo.hcat (K int)\n"), "Completion"));
	/* the same name in a second schema: each table its own, both found */
	CHECK(strstr(f5("ms/x2.sql", "use hcs\nif schema_id('hz') is null exec('create schema hz')\ndrop table if exists hz.hcat\ncreate table hz.hcat (Q int)\n"), "Completion") && !sql_disconnect(&e));
	put("ms/xc.sql", "select 1 as one\n", 0600);
	CHECK(!core_edit_file(&e, "ms/xc.sql", 0) && !sql_connect(&e, "cs") && sql_key(&e, KEY_F5) == 1 && until("Completion time"));
	CHECK(!sql_catalog(&e, e.buf, NULL, NULL, NULL) && catalog(cat) == 2 && !strcmp(cat, "dbo.hcat.K hz.hcat.Q "));
	CHECK(sql_catalog(&e, e.buf, NULL, hcat, (*cat = 0, cat)) == 2 && !strcmp(cat, "dbo.hcat hz.hcat "));
	CHECK(!sql_disconnect(&e));   /* Windows's loop watches 31 handles: a session fewer leaves the catalog one */
	/* NULL before this buffer's login dials nothing: its login reads it */
	put("ms/xd.sql", "select 1 as one\n", 0600);
	CHECK(!core_edit_file(&e, "ms/xd.sql", 0) && !sql_connect(&e, "cs") && !sql_catalog(&e, e.buf, NULL, NULL, NULL));
	for (int i = 0; i < 100; i++) loop_once(10);
	CHECK(!sql_catalog(&e, e.buf, "hcat", hcat, cat) && sql_key(&e, KEY_F5) == 1 && until("Completion time") && catalog(cat) == 2 && !strcmp(cat, "dbo.hcat.K hz.hcat.Q "));
	CHECK(sql_catalog(&e, e.buf, "HCAT", hcat, (*cat = 0, cat)) == 2 && !strcmp(cat, "dbo.hcat.K hz.hcat.Q "));   /* ASCII case ignored, also here */
	put("ms/y.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/y.txt", 0) && !sql_catalog(&e, e.buf, "hcat", hcat, cat));
	put("msx/o.sql", "select 1\n", 0600);
	CHECK(!core_edit_file(&e, "msx/o.sql", 0) && strcpy(e.msg, "kept") && !sql_catalog(&e, e.buf, "hcat", hcat, cat) && !strcmp(e.msg, "kept"));
	/* F5 asks for an undeclared @variable on the command line, the file's
	   own DECLARE as its default; Enter runs; the value is kept, before
	   each batch that needs it, on its first line */
	put("ms/pa.sql", "declare @a int = 3\nselect @a * 2 as d, '@b' as [@c] -- @e\nselect 1/0\ngo\nselect 7 + @a as h\n", 0600);
	CHECK(!core_edit_file(&e, "ms/pa.sql", 0));
	core_goto(&e, 1, 0);
	keys_visual(&e, 'V', 4);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode == MODE_CMDLINE && e.cmdlen == 18 && !memcmp(e.cmd, "Declare @a int = 3", 18));
	typed("\r");
	CHECK(e.mode == MODE_NORMAL && strstr(until("Completion time"), "d\t@c\n6\t@b\n") && strstr(res, "Line 2") && strstr(res, "h\n10\n"));
	CHECK(!sql_declare(&e, "@A  int = 5") && !sql_declare(&e, "") && !strcmp(e.msg, "sql: @A int = 5;"));   /* replaced, case ignored */
	core_goto(&e, 1, 0);
	keys_visual(&e, 'V', 1);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode != MODE_CMDLINE && strstr(until("Completion time"), "DECLARE @a int = 5;\n(1 row affected)\nd\t@c\n10\t@b\n"));   /* the value that ran, shown */
	CHECK(!sql_declare(&e, "@a") && !sql_declare(&e, "") && !strcmp(e.msg, "sql: nothing declared"));
	/* Esc forgets the F5: a :Declare typed later keeps its value and runs nothing */
	core_goto(&e, 1, 0);
	keys_visual(&e, 'V', 1);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode == MODE_CMDLINE);
	typed("\x1b:Declare @a int = 9\r");
	for (int i = 0; i < 50; i++) loop_once(10);
	CHECK(e.mode == MODE_NORMAL && !strstr(results(), "18\t@b") && !sql_declare(&e, "") && !strcmp(e.msg, "sql: @a int = 9;"));
	CHECK(sql_declare(&e, "a int") && strstr(e.msg, ":Declare @name"));
	char longname[160] = "@";
	memset(longname + 1, 'x', 129);
	CHECK(sql_declare(&e, longname) && strstr(e.msg, ":Declare @name"));   /* 128 bytes at most, as the server's names */
	CHECK(!sql_disconnect(&e));   /* the loop's watches are few: each test's session ends */
	/* a DECLARE across lines, with a comment and a ;: typed as one line;
	   a non-ASCII default as its characters; none when the command line
	   cannot hold it or it holds a line break */
	put("ms/pc.sql", "declare @m int -- the id\n\t= 4;\nselect @m as m, @n as n, len(@n) as l, @s as s, (select count(*) from @t) as t, len(@w) as w\n"
		"declare @n nvarchar(20) = N'caf\xc3\xa9 \xe2\x82\xac', @s varchar(9) = 'a\nb', @t table (", 0600);
	FILE *pc = fopen("ms/pc.sql", "a");
	for (int i = 0; i < 40; i++) fprintf(pc, "%scolumn%d int", i ? ", " : "", i);
	fprintf(pc, ")\ndeclare @w varchar(300) = '%0*d'\n", 224, 0);   /* the prompt 252 bytes: as much as keys takes */
	fclose(pc);
	CHECK(!core_edit_file(&e, "ms/pc.sql", 0));
	core_goto(&e, 2, 0);
	keys_visual(&e, 'V', 2);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode == MODE_CMDLINE && e.cmdlen == 18 && !memcmp(e.cmd, "Declare @m int = 4", 18));
	typed("\r");
	CHECK(e.mode == MODE_CMDLINE && e.cmdlen == 38 && !memcmp(e.cmd, "Declare @n nvarchar(20) = N'caf\xc3\xa9 \xe2\x82\xac'", 38));
	typed("\r");
	CHECK(e.mode == MODE_CMDLINE && e.cmdlen == 11 && !memcmp(e.cmd, "Declare @s ", 11));
	typed("varchar(9) = 'x'\r");
	CHECK(e.mode == MODE_CMDLINE && e.cmdlen == 11 && !memcmp(e.cmd, "Declare @t ", 11));
	typed("table (x int)\r");
	CHECK(e.mode == MODE_CMDLINE && e.cmdlen == 252 && !memcmp(e.cmd, "Declare @w varchar(300) = '000", 30));
	typed("\r");
	CHECK(strstr(until("Completion time"), "m\tn\tl\ts\tt\tw\n4\tcaf\xc3\xa9 \xe2\x82\xac\t6\tx\t0\t224\n") && !sql_disconnect(&e));
	/* one byte more: no default, rather than one typed cut */
	put("ms/pg2.sql", "declare @w varchar(300) = '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
		"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'\nselect @w\n", 0600);
	CHECK(!core_edit_file(&e, "ms/pg2.sql", 0));
	core_goto(&e, 1, 0);
	keys_visual(&e, 'V', 1);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode == MODE_CMDLINE && e.cmdlen == 11 && !memcmp(e.cmd, "Declare @w ", 11));
	typed("\x1b");
	/* :Declare is for SQL buffers */
	put("ms/y2.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "ms/y2.txt", 0) && sql_declare(&e, "@a int = 1") && strstr(e.msg, "is for a .sql buffer"));
	/* Postgres has no @variables: @x is its abs() */
	CHECK(strstr(f5("pg/pd.sql", "select @n as a from (select -3 as n) t\n"), "a\n3\n") && e.mode != MODE_CMDLINE && !sql_disconnect(&e));
	/* declared after its use: the server's error, nothing asked */
	CHECK(strstr(f5("ms/pb.sql", "select @q as q\ndeclare @q int\n"), "Must declare the scalar variable \"@q\"") && e.mode != MODE_CMDLINE && !sql_disconnect(&e));
	/* the whole file declares it: nothing asked; a procedure's parameters are its own */
	put("ms/pp.sql", "create or alter procedure #hp @x int as select @x as x\ngo\nexec #hp @x = 4\ngo\ndeclare @k int = 6\ngo\nselect @k as k\n", 0600);
	CHECK(!core_edit_file(&e, "ms/pa.sql", 0) && sql_key(&e, KEY_F5) == 1 && e.mode != MODE_CMDLINE && until("Completion time") && !sql_disconnect(&e));
	CHECK(!core_edit_file(&e, "ms/pp.sql", 0));
	keys_visual(&e, 'V', 2);   /* the procedure and its EXEC: nothing asked */
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode != MODE_CMDLINE && strstr(until("Completion time"), "x\n4\n"));
	typed("\x1b");
	core_goto(&e, 6, 0);   /* a DECLARE after a procedure is still the default */
	keys_visual(&e, 'V', 6);
	CHECK(sql_key(&e, KEY_F5) == 1 && e.mode == MODE_CMDLINE && e.cmdlen == 18 && !memcmp(e.cmd, "Declare @k int = 6", 18));
	typed("\r");
	CHECK(strstr(until("Completion time"), "k\n6\n"));
	/* not a SQL buffer: F5 is not ours */
	put("x.txt", "x\n", 0600);
	CHECK(!core_edit_file(&e, "x.txt", 0) && !sql_key(&e, KEY_F5));
	core_free(&e);
#ifdef _WIN32   /* winpass.sh's summary line */
	fprintf(stderr, "test_sql_editor: F5, GO, a selection, Alt+Break, the results split, :Connect\n");
#endif
	return 0;
}
