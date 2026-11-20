/* libFuzzer: the infoview's JSON scanner (src/infoview/scan.c) on any
   bytes, as Lean or a page could send them: it never reads outside them,
   j_skip ends inside them, j_member and j_elem point at a value that j_skip
   can end; and against its writer: any bytes put through j_quote, as a
   JSON string, decode back to themselves and skip as one value. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/infoview/scan.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
	/* a copy without a terminator after it: ASan sees any read past n */
	char *s = malloc(n ? n : 1);
	if (!s) return 0;
	memcpy(s, data, n);
	size_t e = j_skip(s, n, 0);
	if (e != NONE && (e == 0 || e > n)) abort();
	const char *keys[] = { "id", "m", "a", "method", "params", "uri", "" };
	for (size_t k = 0; k < sizeof keys / sizeof *keys; k++) {
		size_t v = j_member(s, n, 0, keys[k]);
		if (v != NONE && (v >= n || j_skip(s, n, v) == NONE)) abort();
	}
	for (int k = 0; k < 4; k++) {
		size_t v = j_elem(s, n, 0, k);
		if (v != NONE && (v >= n || j_skip(s, n, v) == NONE)) abort();
		size_t len;
		char *d = j_str(s, n, v, &len);
		if (d && v == NONE) abort();
		free(d);
	}
	/* round trip */
	Str q = { 0 };
	str_add(&q, "\"", 1);
	j_quote(&q, s, n);
	str_add(&q, "\"", 1);
	size_t len;
	char *d = q.s ? j_str(q.s, q.n, 0, &len) : NULL;
	if (q.s && (!d || len != n || memcmp(d, s, n) || j_skip(q.s, q.n, 0) != q.n)) abort();
	free(d);
	free(q.s);
	free(s);
	return 0;
}
