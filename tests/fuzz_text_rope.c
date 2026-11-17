/* libFuzzer: the rope's shape and bytes against an array model, with edits
   of up to 8 KB so the tree grows past one level (fuzz_text's edits of a few
   bytes never fill a leaf).  Compiles src/text/text.c again under other
   names to see its nodes, as test_text_rope does. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define text_new r_new
#define text_free r_free
#define text_len r_len
#define text_insert r_insert
#define text_delete r_delete
#define text_read r_read
#define text_lines r_lines
#define text_line_start r_line_start
#define text_line_of r_line_of
#include "../src/text/text.c"

static void shape(const Kid *k, int h, int root)
{
	const Node *x = k->p;
	size_t c = h ? K : B;
	if (x->n > c || (root ? h && x->n < 2 : x->n < c / 2)) abort();
	Kid s = { k->p, 0, 0 };
	sum(&s, h);
	if (s.len != k->len || s.nl != k->nl) abort();
	if (h) for (size_t i = 0; i < x->n; i++) shape(&x->k[i], h - 1, 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char m[1 << 20], b[1 << 20], s[1 << 13];
	size_t mn = 0;
	Text *t = r_new("", 0);
	if (!t) abort();
	for (size_t i = 0; i + 4 <= n; i += 4) {
		size_t pos = (size_t)(d[i + 1] << 8 | d[i + 2]) * (mn + 1) >> 16;
		size_t k = d[i + 3] < 128 ? d[i + 3] % 16 : (size_t)(d[i + 3] - 127) * 64;
		if (d[i] & 1) {
			if (mn + k > sizeof m) continue;
			for (size_t j = 0; j < k; j++) s[j] = "ab\n\xc3\xa9xyz"[(d[i] + j * 7) % 8];
			if (r_insert(t, pos, s, k)) abort();
			memmove(m + pos + k, m + pos, mn - pos); memcpy(m + pos, s, k); mn += k;
		} else {
			if (k > mn - pos) k = mn - pos;
			if (r_delete(t, pos, k)) abort();
			memmove(m + pos, m + pos + k, mn - pos - k); mn -= k;
		}
		shape(&t->r, t->h, 1);
		if (r_len(t) != mn || r_read(t, 0, b, sizeof b) != mn || memcmp(b, m, mn)) abort();
		size_t l = r_line_of(t, pos), ls = r_line_start(t, l), nl = 0;
		for (size_t j = 0; j < pos; j++) nl += m[j] == '\n';
		if (l != nl || ls > pos || (l && m[ls - 1] != '\n') || memchr(m + ls, '\n', pos - ls)) abort();
	}
	r_free(t);
	return 0;
}
