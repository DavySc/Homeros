/* libFuzzer: T-SQL's and JSON's colours (src/syntax/tsql.c, json.c) on text, edits and
   windows decoded from the input, raw bytes included, so every lexing path
   meets every byte, and runs of spaces in 1 KB steps, so the text passes
   the 4,096 bytes between kept token starts.  Each token advances and ends inside the text, and a
   window read through the kept token starts equals a fresh read from 0. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.h"
#include "text.h"
#include "tsql.h"

static const char *const tok[40] = {
	"'", "N'", "\"", "[", "]", "/*", "*/", "--", "\n", "<", ">", "=", "@@ERROR", "LEFT(", "sys.", "x ",
	"1", "0x", "e", "E", "+", "-", ".", "as", "f", "9", " ", "|", "*", "N", "@", "#",
	"\\", ":", "{", "}", "//", "true", "null", ",",
};
static const uint8_t *d;
static size_t n, at;
static unsigned byte(void) { return at < n ? d[at++] : 0; }

enum { MAX = 1 << 15 };       /* past FAR, where a far window is left plain */
static int stop(void) { return byte() & 1; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	d = data, n = size, at = 0;
	int json = byte() & 1;
	const char *path = json ? "f.json" : "f.sql";
	static char b[MAX];
	size_t bn = 0;
	for (unsigned k = byte() % 64; k--; ) {
		unsigned c = byte();
		const char *s = c < 128 ? tok[c % 40] : (const char *)&d[at - 1];
		size_t l = c < 128 ? strlen(s) : 1;
		if (bn + l < sizeof b) memcpy(b + bn, s, l), bn += l;
	}
	Text *t = text_new(b, bn);
	Syntax *s = t ? syntax_open(path, t) : NULL;
	if (!s) abort();
	static unsigned char f[MAX], g[MAX];
	while (at < n) {
		unsigned op = byte();
		size_t len = text_len(t);
		if (op % 4 == 0) syntax_update(s, t, stop);
		else if (op % 4 == 1) {            /* a window, then every token */
			size_t a = byte() * len / 256, z = a + byte() * 8;
			if (z > len) z = len;
			/* drawn again when syntax_update says, as the main loop does */
			syntax_faces(s, a, z, f);
			if (syntax_update(s, t, NULL) == 1) syntax_faces(s, a, z, f);
			Syntax *r = syntax_open(path, t);
			if (!r) abort();
			syntax_update(r, t, NULL);    /* so no far window is left plain */
			for (size_t i = 0; i < z; i += 8192) syntax_faces(r, i, i + 8192 < z ? i + 8192 : z, g + i);
			syntax_free(r);
			if (memcmp(f, g + a, z - a)) abort();
			static TsqlRd rd;
			rd.t = t, rd.n = 0;
			for (size_t p = 0, e; p < len; p = e) {
				int k;
				e = (json ? json_token : tsql_token)(&rd, p, &k);
				if (e <= p || e > len) abort();
			}
		} else {                           /* an edit */
			size_t pos = byte() * len / 256, del = byte() % 8;
			if (del > len - pos) del = len - pos;
			unsigned c = byte();
			const char *ins = c < 128 ? tok[c % 40] : (const char *)&d[at - 1];
			size_t il = c < 128 ? strlen(ins) : 1;
			static char sp[4096];
			if (c == 127) ins = memset(sp, ' ', sizeof sp), il = (byte() % 4 + 1) << 10;
			if (len + il > MAX) continue;
			syntax_edit(s, t, pos, del, ins, il);
			if (text_delete(t, pos, del) || text_insert(t, pos, ins, il)) abort();
		}
	}
	syntax_free(s);
	text_free(t);
	return 0;
}
