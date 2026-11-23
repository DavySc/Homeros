/* T-SQL's colours (src/syntax/tsql.c).  Directed lines with the face of
   every byte, one per SSMS rule tsql.c copies (the categories are SSMS's;
   which words fall in which is checked against SSMS only for the words of
   ssms-check.sql); tsql_token's kinds where SQL Server's lexing is
   subtle; Soufflé's lines; then random texts and edits, T-SQL, JSON and
   Soufflé, where the
   faces read through the kept token starts, in any order and window, must
   equal a fresh read from byte 0. */
#include <string.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"
#include "tsql.h"

static size_t r1, r2;   /* syntax_update's changed fold rows, unused here */

/* K keyword, S string, C comment, F system function, T system table, view
   or schema, P stored procedure, N number, o operator, . plain; Soufflé's
   M modifier, Y type, w keyword operator, B boolean, L constant, V
   directive, E escape, D delimiter, R bracket */
static char code(unsigned char f)
{
	return f == FACE_KEYWORD ? 'K' : f == FACE_STRING ? 'S' : f == FACE_COMMENT ? 'C'
	     : f == FACE_FUNCTION_BUILTIN ? 'F' : f == FACE_MODULE_BUILTIN ? 'T' : f == FACE_FUNCTION ? 'P'
	     : f == FACE_NUMBER ? 'N' : f == FACE_OPERATOR ? 'o' : f == FACE_NONE ? '.'
	     : f == FACE_KEYWORD_MODIFIER ? 'M' : f == FACE_TYPE ? 'Y' : f == FACE_KEYWORD_OPERATOR ? 'w'
	     : f == FACE_BOOLEAN ? 'B' : f == FACE_CONSTANT_BUILTIN ? 'L' : f == FACE_KEYWORD_DIRECTIVE ? 'V'
	     : f == FACE_STRING_ESCAPE ? 'E' : f == FACE_PUNCTUATION_DELIMITER ? 'D' : f == FACE_PUNCTUATION_BRACKET ? 'R' : '?';
}

static void faces_of(const char *path, const char *src, char *out)
{
	size_t n = strlen(src);
	static unsigned char f[256];
	Text *t = text_new(src, n);
	CHECK(t);
	Syntax *s = syntax_open(path, t);
	CHECK(s);
	syntax_faces(s, 0, n, f);
	for (size_t i = 0; i < n; i++) out[i] = code(f[i]);
	out[n] = 0;
	syntax_free(s);
	text_free(t);
}

static const char *const lines[][2] = {
	{ "DECLARE @id int = 5;", "KKKKKKK.....KKK.o.No" },
	{ "select [select], \"from\" From t", "KKKKKK.........o........KKKK.." },
	{ "WHERE a = N'it''s' + 'x'", "KKKKK...o.SSSSSSSS.o.SSS" },
	{ "x /* a /* b */ c */ y -- z", "..CCCCCCCCCCCCCCCCC...CCCC" },
	{ "a <> b >= c != d += 1 << 2", "..oo...oo...oo...oo.N.oo.N" },
	{ "INNER JOIN t ON a LIKE b AND NOT c IS NULL", "ooooo.oooo...KK...oooo...ooo.ooo...oo.oooo" },
	/* a word in two lists has one colour (hypothesis but for LEFT JOIN, day
	   and DAY(, which Davy's screenshot shows) */
	{ "LEFT JOIN t; LEFT(x, 2)", "oooo.oooo..o.ooooo.o.No" },
	{ "DATEADD(day, 1, x), DAY(x)", "FFFFFFFoFFFo.No..oo.FFFo.o" },
	{ "SELECT GETDATE(), COUNT (*), @@ROWCOUNT, @@nope", "KKKKKK.FFFFFFFooo.FFFFF.oooo.FFFFFFFFFFo......." },
	{ "CHAR(10) + CONVERT(int, x); DECLARE @c char", "KKKKoNNo.o.FFFFFFFoKKKo..oo.KKKKKKK....KKKK" },
	{ "FROM sys.objects o JOIN INFORMATION_SCHEMA.TABLES", "KKKK.TTTTTTTTTTT...oooo.TTTTTTTTTTTTTTTTTTTTTTTTT" },
	{ "EXEC sp_executesql @q; EXEC dbo.usp_x", "KKKK.PPPPPPPPPPPPP...o.KKKK.........." },
	/* sp_ alone is a name; a word of 22 bytes is looked up; sys at byte 0 */
	{ "sp_ current_transaction_id", "....FFFFFFFFFFFFFFFFFFFFFF" },
	{ "sys.x", "TTTTT" },
	{ "SET NOCOUNT ON;\nGO", "KKK.KKKKKKK.KKo.KK" },
	{ "SELECT 1.5e3, 0x1F, #t, ##g", "KKKKKK.NNNNNo.NNNNo...o...." },
	/* a number ends where SQL Server ends it, so the word after is coloured */
	{ "SELECT 1as x, 2.5full", "KKKKKK.NKK..o.NNNoooo" },
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
	{ "    DECLARE @n int = 0;  -- row count", "....KKKKKKK....KKK.o.No..CCCCCCCCCCCC" },
	{ "    SELECT TOP (10) o.id, o.total, ISNULL(o.note, N'none') AS [note]", "....KKKKKK.KKK.oNNo.....o........o.FFFFFFo......o.SSSSSSSo.KK......." },
	{ "    FROM dbo.orders AS o", "....KKKK............KK.." },
	{ "    LEFT JOIN dbo.customers c ON c.id = o.customer_id", "....oooo.oooo.................KK......o.............." },
	{ "    WHERE o.customer_id = @customer_id", "....KKKKK...............o............." },
	{ "      AND o.created >= COALESCE(@since, DATEADD(day, -30, GETDATE()))", "......ooo...........oo.FFFFFFFFo......o.FFFFFFFoFFFo.oNNo.FFFFFFFoooo" },
	{ "      AND o.status NOT IN ('void', 'it''s')", "......ooo...KKKKKK.ooo.oo.oSSSSSSo.SSSSSSSo" },
	{ "    ORDER BY o.created DESC;", "....KKKKK.KK...........KKKKo" },
	{ "    SET @n = @@ROWCOUNT;", "....KKK....o.FFFFFFFFFFo" },
	{ "    IF @n = 0 EXEC sp_executesql N'PRINT ''none''';", "....KK....o.N.KKKK.PPPPPPPPPPPPP.SSSSSSSSSSSSSSSSSo" },
	{ "    SELECT name FROM sys.objects WHERE type = 'U';", "....KKKKKK.KKKK.KKKK.TTTTTTTTTTT.KKKKK.KKKK.o.SSSo" },
	{ "END", "KKK" },
	{ "GO", "KK" },
	/* the lines of ssms-check-2.sql (words in two lists) as his SSMS colours
	   them (screenshot of 2026-11-18, 16:31 UTC) */
	{ "DECLARE @c char(10) = CHAR(65), @n nchar(5) = NCHAR(66);", "KKKKKKK....KKKKoNNo.o.KKKKoNNoo....KKKKKoNo.o.KKKKKoNNoo" },
	{ "CREATE TABLE #t (id int IDENTITY(1, 1), at datetime DEFAULT CURRENT_TIMESTAMP);", "KKKKKK.KKKKK....o...KKK.KKKKKKKKoNo.Noo.KK.KKKKKKKK.KKKKKKK.FFFFFFFFFFFFFFFFFoo" },
	{ "SELECT CONVERT(int, '5'), TRY_CONVERT(int, 'x'), YEAR(GETDATE()), MONTH(GETDATE()),", "KKKKKK.FFFFFFFoKKKo.SSSoo.FFFFFFFFFFFoKKKo.SSSoo.FFFFoFFFFFFFoooo.FFFFFoFFFFFFFoooo" },
	{ "       DATEPART(year, GETDATE()), DATEPART(month, GETDATE()), LOG(10),", ".......FFFFFFFFoFFFFo.FFFFFFFoooo.FFFFFFFFoFFFFFo.FFFFFFFoooo.FFFoNNoo" },
	{ "       RIGHT('abc', 2), REPLACE('a', 'b', 'c'), NULLIF(1, 1), CHECKSUM(1),", ".......ooooooSSSSSo.Noo.FFFFFFFoSSSo.SSSo.SSSoo.FFFFFFoNo.Noo.FFFFFFFFoNoo" },
	{ "       CURRENT_USER, SESSION_USER, SYSTEM_USER, CURRENT_TIME, \"quoted\"", ".......FFFFFFFFFFFFo.FFFFFFFFFFFFo.FFFFFFFFFFFo.FFFFFFFFFFFFo........." },
	{ "FROM dbo.a RIGHT JOIN dbo.b ON a.id = b.id", "KKKK.......ooooo.oooo.......KK......o....." },
	{ "CROSS APPLY OPENJSON(N'[1]') j", "ooooo.ooooo.KKKKKKKKoSSSSSSo.." },
	{ "GROUP BY a.id", "KKKKK.KK....." },
	{ "OPTION (RECOMPILE);", "KKKKKK.oKKKKKKKKKoo" },
	/* CONCAT_WS as CONCAT (Davy, 2026-11-19), and the other newer built-ins */
	{ "SELECT CONCAT(a), CONCAT_WS(',', a), DATEDIFF_BIG(day, a, b)", "KKKKKK.FFFFFFo.oo.FFFFFFFFFoSSSo..oo.FFFFFFFFFFFFoFFFo..o..o" },
};

/* Soufflé's, by scanner.ll's tokens (hypothesis: the faces, which no
   reference editor gives); a dot before a directive's word is the
   directive's, wherever it is */
static const char *const dlines[][2] = {
	{ ".decl edge(x:number, y:symbol)", "KKKKK.PPPPR.DYYYYYYD..DYYYYYYR" },
	{ ".output path // done", "KKKKKKK......CCCCCCC" },
	{ "path(x, y) :- edge(x, z), !path(z, \"a\\\"b\").", "PPPPR.D..R.oo.PPPPR.D..RD.oPPPPR.D.SSEESSRD" },
	{ ".decl r(a:unsigned) eqrel btree output brie", "KKKKK.PR.DYYYYYYYYR.MMMMM.MMMMM.MMMMMM.MMMM" },
	{ "n(c) :- c = count : { e(_, _) }, c >= 0x1F, c != 1.5, 10.0.0.1 < 7u.", "PR.R.oo...o.FFFFF.D.R.PR.D..R.RD...oo.NNNND...oo.NNND.NNNNNNNN.o.NND" },
	{ "x = cat(to_string(a), \"s\") band 3 bshru 1 as number", "..o.FFFRFFFFFFFFFR.RD.SSSR.wwww.N.wwwww.N.ww.YYYYYY" },
	{ "#include \"base.dl\"\n.type T <: symbol | Count", "VVVVVVVVVVVVVVVVVV.KKKKK...oo.YYYYYY.o......" },
	{ "/* a\n b */ true false nil __LINE__ type -> .x x.decl", "CCCCCCCCCC.BBBB.BBBBB.LLL.LLLLLLLL......oo.D...KKKKK" },
	{ ".comp C<X> { .init c = C<number> }", "KKKKK..o.o.R.KKKKK...o..oYYYYYYo.R" },
	/* a relation's paren may follow blanks on its line; a string spans lines */
	{ "a (b) :- c\t(d).", "P.R.R.oo.P.R.RD" },
	{ "a\n(b).", "..R.RD" },
	{ "\"a\nb\" x", "SSSSS.." },
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
	{ "1as", 0, 1, TQ_NUMBER },
	{ "2.5full", 0, 3, TQ_NUMBER },
	{ "1e3select", 0, 3, TQ_NUMBER },
	{ "1E-3x", 0, 4, TQ_NUMBER },
	{ "1e+x", 0, 1, TQ_NUMBER },
	{ "1..2", 0, 1, TQ_NUMBER },
	{ "1.e", 0, 2, TQ_NUMBER },
	{ "0x1Fg", 0, 4, TQ_NUMBER },
	{ "a.b", 1, 2, TQ_PUNCT },
	{ "(x", 0, 1, TQ_PUNCT },
	{ ">>=", 0, 2, TQ_OP },
	{ "  \r\n x", 0, 5, TQ_SPACE },
	{ "", 0, 0, TQ_SPACE },
};

static const char *const snip[] = {
	"\n", " ", "'", "N'", "''", "\"", "[", "]", "/*", "*/", "--", "(", ")", ",", ";", ".",
	"<>", "SELECT", " FROM ", "sys", "LEFT", "GETDATE", "@@ERROR", "@x", "#t", "sp_who", "12", "\xc3\xa9", "e", "+", "0x", "as",
	"BEGIN\n  SET NOCOUNT ON;\n", "END\nGO\n", "x",
}, *const jsnip[] = {
	"\n", " ", "\"", "\\", "\\\"", "{", "}", "[", "]", ",", ":", "//", "/*", "*/", "-", "1", "2.5", "e", "E",
	"true", "null", "fals", "\"key\": ", "x", "\xc3\xa9", "0",
}, *const dsnip[] = {
	"\n", " ", ".", "decl", ".decl ", ".output ", "output", "(", ")", ",", ":-", ":", "-", ">", "<", "=", "!",
	"\"", "\\", "\\\"", "//", "/*", "*/", "#", "#include \"x\"\n", "1", "0x1F", "1.5", "u", "edge", "count",
	"number", "x", "_", "?y", "@", "[", "]", "{", "}", ";", "e", "\xc3\xa9",
};
enum { MAXT = 1 << 17 };
/* the random texts' language, as tsql_new's: half T-SQL, a quarter JSON
   and a quarter Soufflé, on the same index */
static int lang;
#define PATH ((const char *const[]){ "x.sql", "x.json", "x.dl" }[lang])

/* snippets into b until want bytes, and never past cap */
static size_t gen(char *b, size_t want, size_t cap)
{
	size_t n = 0;
	while (n < want) {
		const char *s = lang == 2 ? dsnip[rnd(sizeof dsnip / sizeof *dsnip)] : lang ? jsnip[rnd(sizeof jsnip / sizeof *jsnip)] : snip[rnd(sizeof snip / sizeof *snip)];
		size_t l = strlen(s);
		if (n + l > cap) break;
		memcpy(b + n, s, l);
		n += l;
	}
	return n;
}

static int stop_rnd(void) { return !rnd(2); }

/* the faces of the whole text, read once from byte 0 by a fresh Syntax */
static void fresh(const Text *t, unsigned char *f)
{
	Syntax *s = syntax_open(PATH, t);
	CHECK(s);
	syntax_update(s, t, NULL, &r1, &r2);
	size_t n = text_len(t);
	for (size_t a = 0; a < n; a += 8192) syntax_faces(s, a, a + 8192 < n ? a + 8192 : n, f + a);
	syntax_free(s);
}

/* An insertion at a kept token start, or two bytes after it, can join it
   to the token before: "-" then "-b" is a comment, "1e+" then "3" a number.
   The index keeps a start every 4,096 bytes, the first at the first token
   from byte 4,096: mid is put at 4,095, so that start is mid's second
   byte. */
static void edit_near_start(const char *mid, size_t z0, size_t at, const char *ins)
{
	static char b[4200];
	static unsigned char f[4200], g[4200];
	memset(b, ' ', sizeof b);
	b[0] = 'x';
	size_t n = 4095 + strlen(mid), il = strlen(ins);
	memcpy(b + 4095, mid, strlen(mid));
	Text *t = text_new(b, n);
	Syntax *s = syntax_open("x.sql", t);
	CHECK(t && s);
	syntax_update(s, t, NULL, &r1, &r2);
	syntax_faces(s, 0, z0, f);
	syntax_edit(s, t, at, 0, ins, il);
	CHECK(!text_insert(t, at, ins, il));
	syntax_faces(s, 4096, n + il, f + 4096);
	fresh(t, g);
	CHECK(!memcmp(f + 4096, g + 4096, n + il - 4096));
	syntax_free(s);
	text_free(t);
}

int main(void)
{
	char got[256];
	for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
		faces_of("x.sql", lines[i][0], got);
		if (strcmp(got, lines[i][1])) fprintf(stderr, "%s\nwant %s\ngot  %s\n", lines[i][0], lines[i][1], got);
		CHECK(!strcmp(got, lines[i][1]));
	}
	for (size_t i = 0; i < sizeof dlines / sizeof *dlines; i++) {
		faces_of("x.dl", dlines[i][0], got);
		if (strcmp(got, dlines[i][1])) fprintf(stderr, "%s\nwant %s\ngot  %s\n", dlines[i][0], dlines[i][1], got);
		CHECK(!strcmp(got, dlines[i][1]));
	}
	for (size_t i = 0; i < sizeof toks / sizeof *toks; i++) {
		Text *t = text_new(toks[i].src, strlen(toks[i].src));
		int k = -1;
		TsqlRd r = { t, 0, 0, { 0 }, 0 };
		size_t e = tsql_token(&r, toks[i].pos, &k);
		if (e != toks[i].end || k != toks[i].kind) fprintf(stderr, "token %zu: end %zu kind %d\n", i, e, k);
		CHECK(e == toks[i].end && k == toks[i].kind);
		text_free(t);
	}
	edit_near_start("-b\n", 4098, 4096, "-");   /* "-b" becomes a comment */
	edit_near_start("1e+ ", 4099, 4098, "3");   /* "e" and "+" join 1e+3 */
	/* the last token read, "1", ends two bytes before the edit */
	edit_near_start("1e+ ", 4096, 4098, "3");
	/* a NUL starts no two-byte operator (strchr finds the terminator) */
	Text *t0 = text_new("\0=", 2);
	TsqlRd r0 = { t0, 0, 0, { 0 }, 0 };
	int k0;
	CHECK(t0 && tsql_token(&r0, 0, &k0) == 1);
	text_free(t0);
	size_t id, id2;
	CHECK(tsql_word("SeLeCt", 6, &id) == (TQ_KEYWORD | TQ_RECASE) && tsql_word("select", 6, &id2) == (TQ_KEYWORD | TQ_RECASE) && id == id2 && id >= 1 && id <= tsql_nwords);
	CHECK(tsql_word("selectx", 7, &id) == 0 && id == 0 && tsql_word("@@rowcount", 10, NULL) == TQ_FUNCTION);
	/* a formatter's word: known, with no class, so plain */
	CHECK(tsql_word("LIMIT", 5, &id) == 0 && id);
	faces_of("x.sql", "LIMIT 1 OFFSET 2", got);
	CHECK(!strcmp(got, "......N........N"));
	/* whether a token ran to the end unclosed */
	static const struct { const char *src; int open; } opens[] = {
		{ "'a'", 0 }, { "'a", 1 }, { "[a", 1 }, { "\"a\"\"", 1 }, { "/* /* */", 1 }, { "/* */", 0 }, { "x", 0 },
	};
	for (size_t i = 0; i < sizeof opens / sizeof *opens; i++) {
		Text *t = text_new(opens[i].src, strlen(opens[i].src));
		TsqlRd r = { t, 0, 0, { 0 }, 1 };
		int k;
		tsql_token(&r, 0, &k);
		CHECK(r.open == opens[i].open);
		text_free(t);
	}

	/* random texts and edits: what the kept token starts give must equal a
	   fresh read, wherever the windows are asked and the index grows */
	static char b[MAXT];
	static unsigned char f[MAXT], g[MAXT];
	unsigned cases = 800, steps = 0, windows = 0;
	size_t bytes = 0;
	for (unsigned c = 0; c < cases; c++) {
		lang = c % 4 == 3 ? 2 : c % 4 == 2;
		size_t n = rnd(4) ? rnd(600) : rnd(MAXT / 2 - 64);
		n = gen(b, n, MAXT);
		Text *t = text_new(b, n);
		CHECK(t);
		Syntax *s = syntax_open(PATH, t);
		CHECK(s);
		for (int e = 0; e < 30; e++, steps++) {
			n = text_len(t);
			if (e) {
				/* an edit: delete some, insert some snippets */
				size_t p = rnd((unsigned)n + 1), d = rnd(4) ? rnd(8) : rnd(2000);
				if (d > n - p) d = n - p;
				char ins[600];
				size_t ni = rnd(3) ? 1 + rnd(64) : 0;
				ni = gen(ins, ni, sizeof ins);
				if (n - d + ni > MAXT) continue;
				syntax_edit(s, t, p, d, ins, ni);
				CHECK(!text_delete(t, p, d) && !text_insert(t, p, ins, ni));
				n = text_len(t);
			}
			/* the index filled partly, or not, and edits that pile up */
			if (rnd(2)) syntax_update(s, t, stop_rnd, &r1, &r2);
			if (!rnd(3)) continue;
			/* a window line by line from somewhere, as the core asks; drawn
			   again when syntax_update says, as the main loop does */
			size_t wa = n ? rnd((unsigned)n) : 0, z = wa + rnd(3000);
			if (z > n) z = n;
			for (int k = 0; k < 2; k++) {
				for (size_t a = wa; a < z; ) {
					size_t e2 = text_line_start(t, text_line_of(t, a) + 1);
					if (e2 > z) e2 = z;
					if (e2 - a > 1024) e2 = a + 1024;
					syntax_faces(s, a, e2, f + a);
					a = e2;
				}
				int r;
				while ((r = syntax_update(s, t, stop_rnd, &r1, &r2)) == 2) ;
				if (r != 1) break;
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
			bytes += z - wa, windows++;
		}
		syntax_free(s);
		text_free(t);
	}
	printf("test_tsql: %zu lines, %zu Soufflé lines, %zu tokens, %u random texts (a quarter JSON, a quarter Soufflé), %u edits, %u windows (%zu bytes) equal to fresh reads\n",
	       sizeof lines / sizeof *lines, sizeof dlines / sizeof *dlines, sizeof toks / sizeof *toks, cases, steps, windows, bytes);
	return 0;
}
