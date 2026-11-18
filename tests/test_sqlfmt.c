/* T-SQL formatting (src/sqlfmt/).  Directed texts with what SQLDiff's own
   `sqldiff fmt -d tsql --keyword-case lower` printed for them (recorded
   2026-11-18 from DavySc/SQLDiff main, but for its CR LF case: Homeros
   breaks lines with LF, see newline()), one per rule sqlfmt.c ports; a
   range edits only its tokens' gaps; then random token soup, where the
   result keeps every token (keywords may change case only) and, when no
   statement was kept as written, formatting it again changes nothing. */
#include <string.h>
#include "check.h"
#include "text.h"
#include "../src/sqlfmt/sqlfmt.h"

static const char *const cases[][2] = {
	{ "SELECT a,b FROM t WHERE a=1 AND b<>2 ORDER BY a",
	  "select a, b\n"
	  "from t\n"
	  "where a = 1 and b <> 2\n"
	  "order by a\n"
	  "" },
	{ "select o.Id, COUNT(*) from dbo.Orders o inner join Lines l on l.OrderId=o.Id left outer join X x on 1=1 cross apply f(o.Id) group by o.Id having COUNT(*)>1",
	  "select o.Id, COUNT(*)\n"
	  "from dbo.Orders o\n"
	  "inner join Lines l on l.OrderId = o.Id\n"
	  "left outer join X x on 1 = 1\n"
	  "cross apply f(o.Id)\n"
	  "group by o.Id\n"
	  "having COUNT(*) > 1\n"
	  "" },
	{ "SET NOCOUNT ON\n"
	  "DECLARE @x int = 5\n"
	  "UPDATE t SET a = @x WHERE b = 1\n"
	  "DELETE FROM t WHERE a = 2\n"
	  "INSERT INTO t (a, b) SELECT a, b FROM s\n"
	  "EXEC dbo.P @a = 1",
	  "set NOCOUNT on\n"
	  "declare @x int = 5\n"
	  "update t\n"
	  "set a = @x\n"
	  "where b = 1\n"
	  "delete from t\n"
	  "where a = 2\n"
	  "insert into t (a, b)\n"
	  "select a, b\n"
	  "from s\n"
	  "exec dbo.P @a = 1\n"
	  "" },
	{ "IF @a = 1\n"
	  "BEGIN\n"
	  "SELECT 1\n"
	  "END\n"
	  "ELSE IF @a = 2\n"
	  "SELECT 2\n"
	  "ELSE\n"
	  "BEGIN\n"
	  "WHILE @i < 10\n"
	  "SET @i += 1\n"
	  "END",
	  "if @a = 1\n"
	  "begin\n"
	  "    select 1\n"
	  "end\n"
	  "else if @a = 2\n"
	  "    select 2\n"
	  "else\n"
	  "begin\n"
	  "    while @i < 10\n"
	  "        set @i += 1\n"
	  "end\n"
	  "" },
	{ "BEGIN TRY\n"
	  "BEGIN TRAN\n"
	  "UPDATE t SET a = 1\n"
	  "COMMIT\n"
	  "END TRY\n"
	  "BEGIN CATCH\n"
	  "ROLLBACK;\n"
	  "THROW;\n"
	  "END CATCH",
	  "begin TRY\n"
	  "    begin tran\n"
	  "    update t\n"
	  "    set a = 1\n"
	  "    commit\n"
	  "end TRY\n"
	  "begin CATCH\n"
	  "    rollback;\n"
	  "    THROW;\n"
	  "end CATCH\n"
	  "" },
	{ "create procedure dbo.P @a int, @b int = 0 as\n"
	  "begin\n"
	  "set nocount on;\n"
	  "select @a\n"
	  "end\n"
	  "GO\n"
	  "CREATE PROC dbo.Q AS\n"
	  "SELECT 1\n"
	  "SELECT 2\n"
	  "GO",
	  "create procedure dbo.P @a int, @b int = 0 as\n"
	  "begin\n"
	  "    set nocount on;\n"
	  "    select @a\n"
	  "end\n"
	  "GO\n"
	  "create proc dbo.Q as\n"
	  "    select 1\n"
	  "    select 2\n"
	  "GO\n"
	  "" },
	{ "BEGIN\n"
	  "-- first\n"
	  "SELECT 1 -- after\n"
	  "/* block */ SELECT 2\n"
	  "-- before end\n"
	  "END\n"
	  "GO 2\n"
	  "",
	  "begin\n"
	  "    -- first\n"
	  "    select 1 -- after\n"
	  "    /* block */\n"
	  "    select 2\n"
	  "    -- before end\n"
	  "end\n"
	  "GO 2\n"
	  "" },
	{ "SELECT CASE WHEN a = 1 THEN 'x' ELSE 'y' END AS c,\n"
	  "(SELECT MAX(b)\n"
	  "FROM s WHERE s.a = t.a) AS m FROM t",
	  "select case when a = 1 then 'x' else 'y' end as c,\n"
	  "    (select MAX(b)\n"
	  "        from s where s.a = t.a) as m\n"
	  "from t\n"
	  "" },
	{ "UPDATE t SET a*=2, b=-1, c='a'\n"
	  "'b' WHERE x=y",
	  "update t\n"
	  "set a*=2, b = -1, c = 'a'\n"
	  "'b'\n"
	  "where x = y\n"
	  "" },
	{ ":setvar  DB  Test\n"
	  "SELECT t.Select, x.FROM FROM t\n"
	  "lbl: SELECT 1\n"
	  "GOTO lbl",
	  ":setvar  DB  Test\n"
	  "select t.Select, x.FROM\n"
	  "from t\n"
	  "    lbl:\n"
	  "select 1\n"
	  "goto lbl\n"
	  "" },
	{ "SELECT 1\n"
	  "BEGIN\n"
	  "SELECT 2\n"
	  "SELECT 'unclosed",
	  "select 1\n"
	  "BEGIN\n"
	  "SELECT 2\n"
	  "SELECT 'unclosed" },
	{ "\r\n"
	  "\r\n"
	  "SELECT a FROM t   \r\n"
	  "\r\n"
	  "\r\n"
	  "\r\n"
	  "SELECT b FROM u\r\n"
	  "",
	  "select a\n"
	  "from t\n"
	  "\n"
	  "select b\n"
	  "from u\n"
	  "" },
	{ "WITH c AS (SELECT a FROM t) SELECT * FROM c\n"
	  "MERGE INTO t USING s ON t.a = s.a WHEN MATCHED THEN UPDATE SET b = 1 WHEN NOT MATCHED THEN INSERT (a) VALUES (s.a) OUTPUT inserted.a;",
	  "with c as (select a from t)\n"
	  "select *\n"
	  "from c\n"
	  "merge into t\n"
	  "USING s on t.a = s.a\n"
	  "when MATCHED then update\n"
	  "set b = 1\n"
	  "when not MATCHED then insert (a)\n"
	  "values (s.a)\n"
	  "OUTPUT inserted.a;\n"
	  "" },
	{ "select 1; go\n"
	  "select 2",
	  "select 1; go\n"
	  "select 2" },
	{ "SELECT  a,b\n"
	  "BEGIN go\n"
	  "END",
	  "select a, b\n"
	  "BEGIN go\n"
	  "END\n"
	  "" },
	{ "CREATE VIEW v AS SELECT a FROM t WHERE b = 1\n"
	  "GO\n"
	  "CREATE FUNCTION f(@a int) RETURNS int BEGIN RETURN @a END",
	  "create view v as\n"
	  "    select a\n"
	  "    from t\n"
	  "    where b = 1\n"
	  "GO\n"
	  "create function f(@a int) RETURNS int\n"
	  "begin\n"
	  "    return @a\n"
	  "end\n"
	  "" },
};

static int kept;

/* the whole text formatted, applied */
static char *fmt(const char *src, size_t n, size_t a, size_t z, size_t *len)
{
	Text *t = text_new(src, n);
	CHECK(t);
	const char *r;
	size_t m;
	CHECK((kept = sqlfmt(t, &a, &z, &r, &m)) >= 0);
	CHECK(a <= z && z <= n);
	static char out[1 << 16];
	CHECK(n - (z - a) + m < sizeof out);
	memcpy(out, src, a);
	memcpy(out + a, r, m);
	memcpy(out + a + m, src + z, n - z);
	*len = n - (z - a) + m;
	text_free(t);
	return out;
}

static int eq(const char *got, size_t n, const char *want) { return n == strlen(want) && !memcmp(got, want, n); }

/* the bytes outside spaces, tabs and line breaks, lower case */
static size_t squeeze(const char *s, size_t n, char *o)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		if (!strchr(" \t\r\n", s[i])) o[k++] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
	return k;
}

static const char *const soup[] = {
	"select", "FROM", "where", "join", "Left", "on", "and", "or", "not", "group", "by", "order", "insert",
	"into", "values", "update", "set", "delete", "merge", "when", "then", "case", "else", "end", "begin",
	"if", "while", "return", "declare", "exec", "commit", "tran", "try", "catch", "create", "procedure",
	"as", "with", "union", "go", "x", "dbo.t", "NOCOUNT", "@x", "#t", "[a b]", "N'x'", "'a''b'", "1", ",",
	"(", ")", ";", ".", "=", "<>", "*=", ":", "-- c\n", "/* b */", "\n", "\n\n\n", "\r\n", "  ", "\t",
};

int main(void)
{
	size_t n;
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		const char *got = fmt(cases[i][0], strlen(cases[i][0]), 0, strlen(cases[i][0]), &n);
		if (!eq(got, n, cases[i][1])) {
			fprintf(stderr, "case %zu:\n%.*s\n--- want:\n%s\n", i, (int)n, got, cases[i][1]);
			CHECK(0);
		}
	}

	/* a range: only the second statement's gaps and words change */
	const char *two = "SELECT a,b FROM t\nSELECT c,d FROM u\n";
	const char *got = fmt(two, strlen(two), 18, strlen(two), &n);
	CHECK(eq(got, n, "SELECT a,b FROM t\nselect c, d\nfrom u\n"));
	got = fmt(two, strlen(two), 0, 18, &n);
	CHECK(eq(got, n, "select a, b\nfrom t\nSELECT c,d FROM u\n"));

	static char src[4096], again[1 << 16], a[1 << 16], b[1 << 16];
	for (int round = 0; round < 3000; round++) {
		size_t sn = 0;
		for (unsigned k = rnd(80); k--; ) {
			const char *w = soup[rnd(sizeof soup / sizeof *soup)];
			size_t l = strlen(w);
			if (sn + l + 1 >= sizeof src) break;
			memcpy(src + sn, w, l), sn += l;
			if (rnd(4)) src[sn++] = ' ';
		}
		got = fmt(src, sn, 0, sn, &n);
		CHECK(squeeze(src, sn, a) == squeeze(got, n, b) && !memcmp(a, b, squeeze(src, sn, a)));
		if (kept) continue;
		memcpy(again, got, n);
		size_t m;
		const char *twice = fmt(again, n, 0, n, &m);
		if (m != n || memcmp(twice, again, n)) {
			fprintf(stderr, "not stable:\n%.*s\n---\n%.*s\n", (int)n, again, (int)m, twice);
			CHECK(0);
		}
	}
	return 0;
}
