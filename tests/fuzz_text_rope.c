/* libFuzzer: the rope (text.h) against the array in ref/text.c, with edits
   large enough to split, merge and even out nodes on every level of the tree
   (fuzz_text's edits of at most 8 bytes stay inside one leaf). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

/* ref/text.c under other names, so both live in one program */
typedef struct RefText RefText;
#define Text RefText
#define text_new ref_new
#define text_free ref_free
#define text_len ref_len
#define text_insert ref_insert
#define text_delete ref_delete
#define text_read ref_read
#define text_lines ref_lines
#define text_line_start ref_line_start
#define text_line_of ref_line_of
#include "../ref/text.c"
#undef Text
#undef text_new
#undef text_free
#undef text_len
#undef text_insert
#undef text_delete
#undef text_read
#undef text_lines
#undef text_line_start
#undef text_line_of

enum { MAX = 1 << 20 };
static char src[1 << 17], a[MAX], b[MAX];

/* Each op is 4 bytes: kind, two bytes of position, size.  Sizes grow with the
   square of the byte, up to 64 KB, so one op can span many leaves. */
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!src[0])
		for (size_t i = 0; i < sizeof src; i++)   /* lines of 0 to 15 bytes, some empty */
			src[i] = (i * 2654435761u >> 7) % 17 < 2 ? '\n' : (char)('a' + i % 26);
	size_t first = n ? (size_t)d[0] * d[0] : 0;
	Text *t = text_new(src, first);
	RefText *r = ref_new(src, first);
	if (!t || !r) abort();
	for (size_t i = 1; i + 4 <= n; i += 4) {
		size_t len = ref_len(r), pos = ((size_t)d[i + 1] << 8 | d[i + 2]) * (len + 1) >> 16;
		size_t k = (size_t)d[i + 3] * d[i + 3];
		switch (d[i] % 3) {
		case 0:   /* insert: a slice of src, which shifts its newlines around */
			if (len + k > MAX) break;
			if (text_insert(t, pos, src + d[i] * 97 % 4096, k) || ref_insert(r, pos, src + d[i] * 97 % 4096, k)) abort();
			break;
		case 1:   /* delete */
			if (k > len - pos) k = len - pos;
			if (text_delete(t, pos, k) || ref_delete(r, pos, k)) abort();
			break;
		case 2:   /* out of range: must fail and change nothing */
			if (!text_insert(t, len + 1 + k, src, 1) || !text_delete(t, pos, len - pos + 1)) abort();
			break;
		}
		len = ref_len(r);
		if (text_len(t) != len || text_read(t, 0, a, MAX) != len) abort();
		if (ref_read(r, 0, b, MAX) != len || memcmp(a, b, len)) abort();
		if (text_lines(t) != ref_lines(r)) abort();
		/* a read, a line start and a line lookup at positions from the op */
		size_t p = pos * 7 % (len + 2), m = k * 3 + d[i];
		if (text_read(t, p, a, m) != ref_read(r, p, b, m) || memcmp(a, b, text_read(t, p, a, m))) abort();
		size_t l = (d[i + 2] * 31u + k) % (ref_lines(r) + 3);
		if (text_line_start(t, l) != ref_line_start(r, l)) abort();
		if (text_line_of(t, p) != ref_line_of(r, p)) abort();
	}
	text_free(t);
	ref_free(r);
	return 0;
}
