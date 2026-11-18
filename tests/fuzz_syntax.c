/* libFuzzer: syntax.h on text and edits decoded from the input, in C, Lua
   Markdown (injections, code blocks) and SQL (its scanner's $tag$ strings), with parses stopped and resumed at
   input-chosen points and faces read at any moment, stale trees included.
   Sanitizers catch the memory errors; the faces must be grid.h's. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "syntax.h"
#include "text.h"

static const char *const tok[4][16] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", " ", "\xc3\xa9", "#define A(x) x\n", "int", "FOO", ";" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", " ", "\xc3", "local", "function", "end", "--- @param", "vim.cmd" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "_", "`", "[", "](", ")", "- ", "| a |", "\n\n", "\xa9", "> " },
	{ "\n", "$$", "$a$", "$", "'", "--", "(", ")", ";", " ", "x", "SELECT", "CREATE FUNCTION f() RETURNS int AS ", "DO ", "BEGIN", "END" },
};
static const char *const path[4] = { "f.c", "f.lua", "f.md", "f.sql" };

static const uint8_t *d;
static size_t n, at;
static unsigned byte(void) { return at < n ? d[at++] : 0; }
static int left;
static int stop(void) { return left > 0 && --left == 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	d = data, n = size, at = 0;
	int lang = (int)(byte() % 4);
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
			syntax_update(s, t, left ? stop : NULL);
		} else if (op % 4 == 1) {          /* faces of a range */
			size_t a = byte() * len / 256, z = a + byte() * 8;
			if (z > len) z = len;
			syntax_faces(s, a, z, f);
			for (size_t i = 0; i < z - a; i++) if (f[i] >= FACE_COUNT) abort();
		} else {                           /* an edit, keeping the final '\n' */
			size_t pos = byte() * len / 256, del = byte() % 8;
			if (del > len - 1 - pos) del = len - 1 - pos;
			const char *ins = tok[lang][byte() % 16];
			if (len + strlen(ins) > sizeof f) continue;
			syntax_edit(s, t, pos, del, ins, strlen(ins));
			if (text_delete(t, pos, del) || text_insert(t, pos, ins, strlen(ins))) abort();
		}
	}
	while (syntax_update(s, t, NULL) == 2) ;
	if (syntax_update(s, t, NULL) != 0) abort();
	syntax_faces(s, 0, text_len(t), f);
	for (size_t i = 0; i < text_len(t); i++) if (f[i] >= FACE_COUNT) abort();
	syntax_free(s);
	text_free(t);
	return 0;
}
