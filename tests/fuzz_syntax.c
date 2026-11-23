/* libFuzzer: syntax.h on text and edits decoded from the input, in C, Lua,
   Markdown (injections, code blocks), Rust (its scanner's raw strings
   and nested comments, macros), Nix (its scanner's strings, Lua and
   Rust injected into several strings as one tree), CMake (its scanner's
   bracket arguments) and SystemVerilog, with parses stopped and resumed at
   input-chosen points and faces, syntax_md and folds read at any moment, stale
   trees included.
   Sanitizers catch the memory errors; the faces must be grid.h's, and a
   finished parse changes no fold outside the rows it reports. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "syntax.h"
#include "text.h"
static size_t r1, r2;   /* syntax_update's fold rows, read where a test needs them */

static const char *const tok[7][16] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", " ", "\xc3\xa9", "#define A(x) x\n", "int", "FOO", ";" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", " ", "\xc3", "local", "function", "end", "--- @param", "vim.cmd" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "1. ", "`", "[", "](", ")", "- ", "| a |", "\n\n", "\xa9", "> " },
	{ "\n", "r#\"", "\"#", "\"", "/*", "*/", "//", "{", "}", "(", ")", " ", "x", "println!(", "fn f<'a>(", "'a" },
	{ "\n", "#", "/*", "*/", "''", "\"", "{", "}", "${", ";", "x", "\\", "/* lua */ ''", "# rust\n", "pkgs.writeRust \"a\" {} ''", "''$" },
	{ "\n", "#", "#[[", "]]", "[==[", "]==]", "\"", "${", "}", "(", ")", " ", "x", "\\", "set(", "endif()" },
	{ "\n", "//", "/*", "*/", "\"", "(", ")", ";", "x", " ", "`define A", "`A", "module m;", "endmodule", "begin", "end" },
};
static const char *const path[7] = { "f.c", "f.lua", "f.md", "f.rs", "f.nix", "f.cmake", "f.sv" };

static const uint8_t *d;
static size_t n, at;
static unsigned byte(void) { return at < n ? d[at++] : 0; }
static int left;
static int stop(void) { return left > 0 && --left == 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	d = data, n = size, at = 0;
	int lang = (int)(byte() % 7);
	char b[4096];
	size_t bn = 0;
	for (unsigned k = byte() % 32; k--; ) {
		const char *s = tok[lang][byte() % 16];
		size_t l = strlen(s);
		if (bn + l < sizeof b) memcpy(b + bn, s, l), bn += l;
	}
	b[bn++] = '\n';
	Text *t = text_new(b, bn);
	Syntax *s = t ? syntax_open(path[lang], t) : NULL;
	if (!s) abort();
	static unsigned char f[1 << 13];
	while (at < n) {
		unsigned op = byte();
		size_t len = text_len(t);
		if (op % 4 == 0) {                 /* parse, stopped after some checks */
			left = (int)(byte() % 8);
			static int lv[1 << 13], st[1 << 13];   /* a row outside [r1, r2) keeps its fold */
			size_t nl = text_lines(t);
			for (size_t l = 0; l < nl; l++) lv[l] = syntax_foldexpr(s, l, &st[l]);
			if (syntax_update(s, t, left ? stop : NULL, &r1, &r2) != 2)
				for (size_t l = 0; l < nl; l++) {
					int a, v = syntax_foldexpr(s, l, &a);
					if ((v != lv[l] || a != st[l]) && (l < r1 || l >= r2)) abort();
				}
		} else if (op % 4 == 1) {          /* faces of a range */
			size_t a = byte() * len / 256, z = a + byte() * 8;
			if (z > len) z = len;
			syntax_faces(s, a, z, f);
			for (size_t i = 0; i < z - a; i++) if (f[i] >= FACE_COUNT) abort();
			SyntaxMd m;   /* render-markdown's facts, stale trees included */
			syntax_md(s, t, byte() % (text_lines(t) + 1), &m);
			int st;   /* folds, which parse first */
			if (byte() % 4 == 0 && syntax_foldexpr(s, byte() % (text_lines(t) + 1), &st) > 20 && lang != 2) abort();   /* Markdown: the '#' count */
		} else {                           /* an edit, keeping the final '\n' */
			size_t pos = byte() * len / 256, del = byte() % 8;
			if (del > len - 1 - pos) del = len - 1 - pos;
			const char *ins = tok[lang][byte() % 16];
			if (len + strlen(ins) > sizeof f) continue;
			syntax_edit(s, t, pos, del, ins, strlen(ins));
			if (text_delete(t, pos, del) || text_insert(t, pos, ins, strlen(ins))) abort();
		}
	}
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	if (syntax_update(s, t, NULL, &r1, &r2) != 0) abort();
	syntax_faces(s, 0, text_len(t), f);
	for (size_t i = 0; i < text_len(t); i++) if (f[i] >= FACE_COUNT) abort();
	syntax_free(s);
	text_free(t);
	return 0;
}
