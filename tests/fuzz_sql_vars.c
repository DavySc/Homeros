/* libFuzzer: any text through package 22's script reading (run.c): the GO
   split, and the @variables a batch lacks with its DECLAREs.  Each name
   listed is one @name of 128 bytes at most, once (case ignored), and the
   DECLAREs come in pairs of a name and what follows it. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../src/sql/run.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	Text *t = text_new((const char *)d, n);
	if (!t) return 0;
	SqlBatches b = {0};
	Str o = {0}, v = {0};
	if (sql_split(t, 0, n, &b) >= 0 && !sql_vars(t, 0, n, &o, &v)) {
		for (size_t i = 0, l; i < o.n; i += l + 1) {
			l = strlen(o.s + i);
			if (o.s[i] != '@' || l < 2 || l > 128 || o.s[i + 1] == '@') abort();
			for (size_t j = i + l + 1; j < o.n; j += strlen(o.s + j) + 1) if (!strcasecmp(o.s + i, o.s + j)) abort();
		}
		size_t k = 0;
		for (size_t i = 0; i < v.n; i += strlen(v.s + i) + 1, k++) if (!(k % 2) && v.s[i] != '@') abort();
		if (k % 2 || (v.n && v.s[v.n - 1])) abort();
	}
	free(b.b), free(o.s), free(v.s);
	text_free(t);
	return 0;
}
