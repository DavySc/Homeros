/* JSON's colours (src/syntax/tsql.c) against Neovim (nvim --headless on
   PATH, running tests/test_json.lua) with tree-sitter-json and
   nvim-treesitter's highlights for it (build/syntax/src, from
   src/syntax/sources.nix): random valid documents, comments included,
   read line by line as the core draws, from the first or from the last up,
   must give Neovim's face for every byte.  Then directed lines where the
   colours depart from it on purpose.  Edits and windows through the kept token starts are
   test_tsql's, whose random texts are a third JSON. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"

static size_t r1, r2;   /* syntax_update's changed fold rows, unused here */

enum { DOCS = 300, MAXT = 1 << 15 };

/* test_json.lua's letters */
static char code(unsigned char f)
{
	return f == FACE_BOOLEAN ? 'B' : f == FACE_CONSTANT_BUILTIN ? 'L' : f == FACE_NUMBER ? 'N'
	     : f == FACE_PROPERTY ? 'P' : f == FACE_STRING ? 'S' : f == FACE_STRING_ESCAPE ? 'E'
	     : f == FACE_COMMENT ? 'C' : f == FACE_PUNCTUATION_DELIMITER ? 'D'
	     : f == FACE_PUNCTUATION_BRACKET ? 'R' : f == FACE_NONE ? '.' : '?';
}

/* our letters for the n bytes of b, the lines read from the last up with
   up, so each read starts from a kept token start */
static void ours(const char *b, size_t n, char *out, int up)
{
	static unsigned char f[MAXT];
	Text *t = text_new(b, n);
	Syntax *s = t ? syntax_open("x.json", t) : NULL;
	CHECK(s);
	syntax_update(s, t, NULL, &r1, &r2);
	for (size_t i = 0, nl = text_lines(t); i <= nl; i++) {
		size_t l = up ? nl - i : i, a = text_line_start(t, l), z = text_line_start(t, l + 1);
		/* in pieces of 256 bytes, as draw_line asks */
		for (size_t j = 0, m = (z - a + 255) / 256; j < m; j++) {
			size_t c = a + 256 * (up ? m - 1 - j : j);
			syntax_faces(s, c, c + 256 < z ? c + 256 : z, f + c);
		}
	}
	for (size_t i = 0; i < n; i++) out[i] = code(f[i]);
	out[n] = 0;
	syntax_free(s);
	text_free(t);
}

static char b[MAXT];
static size_t n;

static void put(const char *s)
{
	size_t l = strlen(s);
	if (n + l < MAXT) memcpy(b + n, s, l), n += l;
}

static void pick(const char *const *s, unsigned k) { put(s[rnd(k)]); }

/* spaces, and comments where the grammar takes them; with nocom one line,
   as minified JSON is, and no string holds what starts a T-SQL comment,
   string or name */
static int nocom;
static void ws(void)
{
	static const char *const s[] = { "", "", "", " ", "\t", "\n  ", "\r\n", "// \"k\": 1, /*\n", "/* \"a\": [ */", "/**/", "/* x\n * y **/" };
	pick(s, nocom ? 5 : sizeof s / sizeof *s);
}

static void str(void)
{
	static const char *const s[] = { "a", "key", " ", "\\\"", "\\\\", "\\/", "\\b", "\\n", "\\t", "\\u00e9", "\xc3\xa9", ":", "true", "1", "//", "/*", "'", "[", "--" };
	put("\"");
	/* one in 40 long, so its escapes are read from windows inside it */
	for (unsigned k = rnd(40) ? rnd(5) : 100 + rnd(200); k--; ) pick(s, nocom ? 14 : sizeof s / sizeof *s);
	put("\"");
}

/* tree-sitter-json's number: -?(0|[1-9]\d*)(\.\d*)?([eE]-?\d+)? */
static void num(void)
{
	static const char *const i[] = { "0", "7", "12", "90", "123456789" }, *const f[] = { ".", ".5", ".25", ".000" }, *const e[] = { "e1", "E7", "e-3", "E-10" };
	if (rnd(3) == 0) put("-");
	pick(i, 5);
	if (rnd(2)) pick(f, 4);
	if (rnd(3) == 0) pick(e, 4);
}

static void value(int depth)
{
	static const char *const lit[] = { "true", "false", "null" };
	unsigned r = depth == 0 ? rnd(2) : depth > 5 ? 2 + rnd(3) : rnd(6);
	if (r < 2) {
		put(r ? "[" : "{");
		for (unsigned i = 0, k = rnd(6); i < k; i++) {
			if (i) ws(), put(",");
			ws();
			if (!r) str(), ws(), put(":"), ws();
			value(depth + 1);
		}
		ws();
		put(r ? "]" : "}");
	} else if (r == 2 || r == 5) str();
	else if (r == 3) num();
	else pick(lit, 3);
}

int main(void)
{
	char dir[] = "/tmp/test_json_XXXXXX", path[256], cmd[512];
	static char want[MAXT + 2], got[MAXT + 1];
	CHECK(mkdtemp(dir));
	size_t bytes = 0;
	for (int i = 0; i < DOCS; i++) {
		/* a tenth past 4,096 bytes, so faces are read from kept token
		   starts (those read from the last line up), half of them on
		   one line without comments (a slash-star would hide the rest
		   from a T-SQL lexer) */
		n = 0;
		ws(), value(0), ws();
		if (i % 10 == 1) {
			n = 0, nocom = i % 20 == 1;
			put("[");
			while (n < 20000) value(1), ws(), put(","), ws();
			put("0]");
			nocom = 0;
		}
		snprintf(path, sizeof path, "%s/%d.json", dir, i);
		FILE *o = fopen(path, "wb");
		CHECK(o && fwrite(b, 1, n, o) == n && !fclose(o));
	}
	snprintf(cmd, sizeof cmd, "nvim --headless --clean -l tests/test_json.lua build/syntax/src/json.so build/syntax/src/json.scm %s %d", dir, DOCS);
	CHECK(!system(cmd));
	for (int i = 0; i < DOCS; i++) {
		snprintf(path, sizeof path, "%s/%d.json", dir, i);
		FILE *o = fopen(path, "rb");
		CHECK(o);
		n = fread(b, 1, MAXT, o);
		fclose(o);
		snprintf(path, sizeof path, "%s/%d.want", dir, i);
		o = fopen(path, "rb");
		CHECK(o);
		size_t wn = fread(want, 1, sizeof want - 1, o);
		fclose(o);
		want[wn] = 0;
		if (want[0] == '!') fprintf(stderr, "%s/%d.json: tree-sitter-json finds a syntax error\n", dir, i);
		CHECK(want[0] != '!' && wn == n);
		ours(b, n, got, i % 2);
		for (size_t j = 0; j < n; j++)
			if (got[j] != want[j]) {
				fprintf(stderr, "%s/%d.json: byte %zu: %c, Neovim %c\n%.*s\n", dir, i, j, got[j], want[j], (int)(n - j < 60 ? n - j : 60), b + j);
				break;
			}
		CHECK(!strcmp(got, want));
		bytes += n;
		unlink(path);
		snprintf(path, sizeof path, "%s/%d.json", dir, i);
		unlink(path);
	}
	rmdir(dir);

	/* on purpose: a string outside a pair or an array is a string (Neovim
	   leaves it plain, a document that is only a string); an unclosed
	   comment runs to the end (Neovim finds an error, and every frame would
	   read to the end); an exponent takes a plus (RFC 8259; tree-sitter-json
	   finds an error); a string not closed on its line ends there, escaped
	   or not (tree-sitter-json goes on to a later quote) */
	static const char *const lines[][2] = {
		{ "\"a\\\"\"", "SSEES" },
		{ "[1] /* x", "RNR.CCCC" },
		{ "[1e+5, -2.5E+10]", "RNNNND.NNNNNNNNR" },
		{ "\"a\n1", "SS.N" },
		{ "\"\\\n1", "SS.N" },
	};
	for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
		ours(lines[i][0], strlen(lines[i][0]), got, 0);
		if (strcmp(got, lines[i][1])) fprintf(stderr, "%s\nwant %s\ngot  %s\n", lines[i][0], lines[i][1], got);
		CHECK(!strcmp(got, lines[i][1]));
	}
	printf("test_json: %d random documents (%zu bytes) as Neovim colours them, %zu lines\n", DOCS, bytes, sizeof lines / sizeof *lines);
	return 0;
}
