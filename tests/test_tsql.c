/* T-SQL's colours (src/syntax/tsql.c).  Directed lines with the face of
   every byte, one per SSMS rule tsql.c copies (the categories are SSMS's;
   which words fall in which is checked against SSMS only for the words of
   ssms-check.sql); tsql_token's kinds where SQL Server's lexing is
   subtle; then random texts and edits, where the faces read through the
   kept token starts, in any order and window, must equal a fresh read
   from byte 0. */
#include <string.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"
#include "../src/syntax/tsql.h"

/* K keyword, S string, C comment, F system function, T system table, view
   or schema, P stored procedure, o operator, . plain */
static char code(unsigned char f)
{
	return f == FACE_KEYWORD ? 'K' : f == FACE_STRING ? 'S' : f == FACE_COMMENT ? 'C'
	     : f == FACE_FUNCTION_BUILTIN ? 'F' : f == FACE_MODULE_BUILTIN ? 'T' : f == FACE_FUNCTION ? 'P'
	     : f == FACE_OPERATOR ? 'o' : f == FACE_NONE ? '.' : '?';
}

static void faces_of(const char *src, char *out)
{
	size_t n = strlen(src);
	static unsigned char f[256];
	Text *t = text_new(src, n);
	CHECK(t);
	Syntax *s = syntax_open("x.sql", t);
	CHECK(s);
	syntax_faces(s, 0, n, f);
	for (size_t i = 0; i < n; i++) out[i] = code(f[i]);
	out[n] = 0;
	syntax_free(s);
	text_free(t);
}

static const char *const lines[][2] = {
	{ "DECLARE @id int = 5;", "KKKKKKK.....KKK.o..o" },
	{ "select [select], \"from\" From t", "KKKKKK.........o........KKKK.." },
	{ "WHERE a = N'it''s' + 'x'", "KKKKK...o.SSSSSSSS.o.SSS" },
	{ "x /* a /* b */ c */ y -- z", "..CCCCCCCCCCCCCCCCC...CCCC" },
	{ "a <> b >= c != d += 1", "..oo...oo...oo...oo.." },
	{ "INNER JOIN t ON a LIKE b AND NOT c IS NULL", "ooooo.oooo...KK...oooo...ooo.ooo...oo.oooo" },
	{ "LEFT JOIN t; LEFT(x, 2)", "oooo.oooo..o.FFFFo.o..o" },
	{ "SELECT GETDATE(), COUNT (*), @@ROWCOUNT, @@nope", "KKKKKK.FFFFFFFooo.FFFFF.oooo.FFFFFFFFFFo......." },
	{ "CHAR(10) + CONVERT(int, x); DECLARE @c char", "FFFFo..o.o.FFFFFFFoKKKo..oo.KKKKKKK....KKKK" },
	{ "FROM sys.objects o JOIN INFORMATION_SCHEMA.TABLES", "KKKK.TTTTTTTTTTT...oooo.TTTTTTTTTTTTTTTTTTTTTTTTT" },
	{ "EXEC sp_executesql @q; EXEC dbo.usp_x", "KKKK.PPPPPPPPPPPPP...o.KKKK.........." },
	{ "SET NOCOUNT ON;\nGO", "KKK.KKKKKKK.KKo.KK" },
	{ "SELECT 1.5e3, 0x1F, #t, ##g", "KKKKKK......o.....o...o...." },
	/* the lines of ssms-check.sql as Davy's SSMS colours them (his
	   screenshot of 2026-11-18, read colour by colour) */
	{ "/* Demo: SSMS-style colours, mapped to Catppuccin */", "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC" },
	{ "SET ANSI_NULLS ON;", "KKK.KKKKKKKKKK.KKo" },
	{ "GO", "KK" },
	{ "CREATE OR ALTER PROCEDURE dbo.usp_orders", "KKKKKK.oo.KKKKK.KKKKKKKKK..............." },
	{ "    @customer_id int,", ".................KKKo" },
	{ "    @since datetime2 = NULL", "...........KKKKKKKKK.o.oooo" },
	{ "AS", "KK" },
	{ "BEGIN", "KKKKK" },
	{ "    SET NOCOUNT ON;", "....KKK.KKKKKKK.KKo" },
	{ "    DECLARE @n int = 0;  -- row count", "....KKKKKKK....KKK.o..o..CCCCCCCCCCCC" },
	{ "    SELECT TOP (10) o.id, o.total, ISNULL(o.note, N'none') AS [note]", "....KKKKKK.KKK.o..o.....o........o.FFFFFFo......o.SSSSSSSo.KK......." },
	{ "    FROM dbo.orders AS o", "....KKKK............KK.." },
	{ "    LEFT JOIN dbo.customers c ON c.id = o.customer_id", "....oooo.oooo.................KK......o.............." },
	{ "    WHERE o.customer_id = @customer_id", "....KKKKK...............o............." },
	{ "      AND o.created >= COALESCE(@since, DATEADD(day, -30, GETDATE()))", "......ooo...........oo.FFFFFFFFo......o.FFFFFFFoKKKo.o..o.FFFFFFFoooo" },
	{ "      AND o.status NOT IN ('void', 'it''s')", "......ooo...KKKKKK.ooo.oo.oSSSSSSo.SSSSSSSo" },
	{ "    ORDER BY o.created DESC;", "....KKKKK.KK...........KKKKo" },
	{ "    SET @n = @@ROWCOUNT;", "....KKK....o.FFFFFFFFFFo" },
	{ "    IF @n = 0 EXEC sp_executesql N'PRINT ''none''';", "....KK....o...KKKK.PPPPPPPPPPPPP.SSSSSSSSSSSSSSSSSo" },
	{ "    SELECT name FROM sys.objects WHERE type = 'U';", "....KKKKKK.KKKK.KKKK.TTTTTTTTTTT.KKKKK.KKKK.o.SSSo" },
	{ "END", "KKK" },
	{ "GO", "KK" },
};

/* tsql_token's kinds and ends */
static const struct { const char *src; size_t pos, end; int kind; } toks[] = {
	{ "'a''b' x", 0, 6, TQ_STRING },
	{ "n'x'", 0, 4, TQ_STRING },
	{ "Nx", 0, 2, TQ_WORD },
	{ "[a]]b] c", 0, 6, TQ_IDENT },
	{ "\"a\"\"b\"", 0, 6, TQ_IDENT },
	{ "'open", 0, 5, TQ_STRING },
	{ "/* a /* b */", 0, 12, TQ_COMMENT },
	{ "/* /* */ */x", 0, 11, TQ_COMMENT },
	{ "-- a\nb", 0, 4, TQ_COMMENT },
	{ "-- a\r\nb", 0, 4, TQ_COMMENT },
	{ "||x", 0, 2, TQ_OP },
	{ "!<x", 0, 2, TQ_OP },
	{ "::x", 0, 2, TQ_OP },
	{ "^=", 0, 2, TQ_OP },
	{ "<=>", 0, 2, TQ_OP },
	{ "<>x", 0, 2, TQ_OP },
	{ "- -", 0, 1, TQ_OP },
	{ "@a.b", 0, 2, TQ_VAR },
	{ ".5)", 0, 2, TQ_NUMBER },
	{ "a.b", 1, 2, TQ_PUNCT },
	{ "  \r\n x", 0, 5, TQ_SPACE },
	{ "", 0, 0, TQ_SPACE },
};

static const char *const snip[] = {
	"\n", " ", "'", "N'", "''", "\"", "[", "]", "/*", "*/", "--", "(", ")", ",", ";", ".",
	"<>", "SELECT", " FROM ", "sys", "LEFT", "GETDATE", "@@ERROR", "@x", "#t", "sp_who", "12", "\xc3\xa9",
	"BEGIN\n  SET NOCOUNT ON;\n", "END\nGO\n", "x",
};
enum { NSNIP = sizeof snip / sizeof *snip, MAXT = 1 << 15 };

static size_t gen(char *b, size_t max)
{
	size_t n = 0, want = rnd(4) ? rnd(600) : rnd(max - 64);
	while (n < want) {
		const char *s = snip[rnd(NSNIP)];
		size_t l = strlen(s);
		memcpy(b + n, s, l);
		n += l;
	}
	return n;
}

/* the faces of the whole text, read once from byte 0 by a fresh Syntax */
static void fresh(const Text *t, unsigned char *f)
{
	Syntax *s = syntax_open("x.sql", t);
	CHECK(s);
	size_t n = text_len(t);
	for (size_t a = 0; a < n; a += 8192) syntax_faces(s, a, a + 8192 < n ? a + 8192 : n, f + a);
	syntax_free(s);
}

/* An insertion at a kept token start can join it to the token before:
   "-" then "-b" is a comment.  The index keeps a start every 4,096 bytes,
   the first at the first token from byte 4,096, here the "b". */
static void edit_at_start(void)
{
	static char b[4200];
	static unsigned char f[4200], g[4200];
	memset(b, ' ', sizeof b);
	b[0] = 'x', b[4095] = '-', b[4096] = 'b', b[4097] = '\n';
	size_t n = 4098;
	Text *t = text_new(b, n);
	Syntax *s = syntax_open("x.sql", t);
	CHECK(t && s);
	syntax_update(s, t, NULL);
	syntax_faces(s, 0, n, f);
	syntax_edit(s, t, 4096, 0, "-", 1);
	CHECK(!text_insert(t, 4096, "-", 1));
	syntax_faces(s, 4096, n + 1, f + 4096);
	fresh(t, g);
	CHECK(f[4096] == FACE_COMMENT && !memcmp(f + 4096, g + 4096, n + 1 - 4096));
	syntax_free(s);
	text_free(t);
}

int main(void)
{
	char got[256];
	for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
		faces_of(lines[i][0], got);
		if (strcmp(got, lines[i][1])) fprintf(stderr, "%s\nwant %s\ngot  %s\n", lines[i][0], lines[i][1], got);
		CHECK(!strcmp(got, lines[i][1]));
	}
	for (size_t i = 0; i < sizeof toks / sizeof *toks; i++) {
		Text *t = text_new(toks[i].src, strlen(toks[i].src));
		int k = -1;
		TsqlRd r = { t, 0, 0, { 0 } };
		size_t e = tsql_token(&r, toks[i].pos, &k);
		if (e != toks[i].end || k != toks[i].kind) fprintf(stderr, "token %zu: end %zu kind %d\n", i, e, k);
		CHECK(e == toks[i].end && k == toks[i].kind);
		text_free(t);
	}
	edit_at_start();
	CHECK(tsql_word("SeLeCt", 6) == (TQ_KEYWORD | TQ_RECASE) && tsql_word("selectx", 7) == 0 && tsql_word("@@rowcount", 10) == TQ_FUNCTION);

	/* random texts and edits: what the kept token starts give must equal a
	   fresh read, wherever the windows are asked and the index grows */
	static char b[MAXT];
	static unsigned char f[MAXT], g[MAXT];
	unsigned cases = 400, steps = 0;
	size_t bytes = 0;
	for (unsigned c = 0; c < cases; c++) {
		size_t n = gen(b, MAXT / 2);
		Text *t = text_new(b, n);
		CHECK(t);
		Syntax *s = syntax_open("x.sql", t);
		CHECK(s);
		for (int e = 0; e < 30; e++, steps++) {
			n = text_len(t);
			if (e) {
				/* an edit: delete some, insert some snippets */
				size_t p = rnd((unsigned)n + 1), d = rnd(4) ? rnd(8) : rnd(2000);
				if (d > n - p) d = n - p;
				char ins[600];
				size_t ni = rnd(3) ? 1 + rnd(64) : 0;
				if (ni) {
					size_t m = 0;
					while (m < ni) {
						const char *x = snip[rnd(NSNIP)];
						size_t l = strlen(x);
						if (m + l > sizeof ins) break;
						memcpy(ins + m, x, l), m += l;
					}
					ni = m;
				}
				if (n - d + ni > MAXT) continue;
				syntax_edit(s, t, p, d, ins, ni);
				CHECK(!text_delete(t, p, d) && !text_insert(t, p, ins, ni));
				n = text_len(t);
			}
			if (rnd(2)) syntax_update(s, t, NULL);
			/* a window line by line from somewhere, as the core asks */
			size_t wa = n ? rnd((unsigned)n) : 0, z = wa + rnd(3000);
			if (z > n) z = n;
			for (size_t a = wa; a < z; ) {
				size_t e2 = text_line_start(t, text_line_of(t, a) + 1);
				if (e2 > z) e2 = z;
				if (e2 - a > 1024) e2 = a + 1024;
				syntax_faces(s, a, e2, f + a);
				a = e2;
			}
			fresh(t, g);
			for (size_t i = wa; i < z; i++)
				if (f[i] != g[i]) fprintf(stderr, "case %u step %d: byte %zu of %zu: %c, fresh %c\n", c, e, i, n, code(f[i]), code(g[i]));
			/* SHOW=file keeps a failing text */
			if (memcmp(f + wa, g + wa, z - wa) && getenv("SHOW")) {
				static char tb[MAXT];
				size_t tn = text_read(t, 0, tb, sizeof tb);
				FILE *o = fopen(getenv("SHOW"), "wb");
				fwrite(tb, 1, tn, o);
				fclose(o);
				fprintf(stderr, "window %zu..%zu\n", wa, z);
			}
			CHECK(!memcmp(f + wa, g + wa, z - wa));
			bytes += z - wa;
		}
		syntax_free(s);
		text_free(t);
	}
	printf("test_tsql: %zu lines, %zu tokens, %u random texts, %u edits and windows (%zu bytes) equal to fresh reads\n",
	       sizeof lines / sizeof *lines, sizeof toks / sizeof *toks, cases, steps, bytes);
	return 0;
}
