/* PR 28 review: text_line_start against a byte-at-a-time model, for every
   line of texts whose leaves end at every offset around the 32-byte blocks.
   Leaves of each size 1..1016 come from text_new on that length; other leaf
   sizes and block alignments from inserts and deletes.  Newline patterns: a
   '\n' every p bytes at each phase (p = 1..70 covers 32, 33, 64 and their
   neighbours), all '\n', none, only the last byte, only the first byte of the
   last block, and random densities. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
enum { MAX = 1 << 16 };
static char m[MAX];
static unsigned long long checks;
static unsigned s = 1;
static unsigned rnd(unsigned n) { s = s * 1103515245u + 12345u; return (s >> 16) % n; }
static void all(const Text *t, size_t n)
{
	size_t l = 0;
	if (text_line_start(t, 0) != 0) { printf("FAIL l=0\n"); exit(1); }
	for (size_t i = 0; i < n; i++)
		if (m[i] == '\n') {
			l++; checks++;
			if (text_line_start(t, l) != i + 1) { printf("FAIL n=%zu l=%zu want %zu got %zu\n", n, l, i + 1, text_line_start(t, l)); exit(1); }
		}
	if (text_lines(t) != l || text_line_start(t, l + 1) != n) { printf("FAIL end n=%zu\n", n); exit(1); }
}
static void one(size_t n) { Text *t = text_new(m, n); if (!t) exit(2); all(t, n); text_free(t); }
int main(void)
{
	for (size_t n = 0; n <= 2200; n++) {
		memset(m, '\n', n); one(n);
		memset(m, 'a', n); one(n);
		if (n) { m[n - 1] = '\n'; one(n); m[n - 1] = 'a'; }
		if (n) { m[(n - 1) / 32 * 32] = '\n'; one(n); }
		if (n > 32) { memset(m, 'a', n); m[n - 33] = '\n'; one(n); m[n - 32] = '\n'; one(n); }
	}
	for (size_t n = 990; n <= 1100; n++)
		for (size_t p = 1; p <= 70; p++)
			for (size_t ph = 0; ph < p; ph++) {
				for (size_t i = 0; i < n; i++) m[i] = i % p == ph ? '\n' : 'a';
				one(n);
			}
	for (size_t n = 1; n <= 140; n++)
		for (size_t p = 1; p <= 70; p++)
			for (size_t ph = 0; ph < p; ph++) {
				for (size_t i = 0; i < n; i++) m[i] = i % p == ph ? '\n' : 'a';
				one(n);
			}
	/* leaves of arbitrary size and alignment: random edits, then every line */
	for (int round = 0; round < 300; round++) {
		size_t n = rnd(6000);
		unsigned d = 1 + rnd(40);
		for (size_t i = 0; i < n; i++) m[i] = rnd(d) ? 'a' : '\n';
		Text *t = text_new(m, n);
		for (int e = 0; e < 200; e++) {
			if (rnd(2) && n + 80 < MAX) {
				char ins[80]; size_t k = 1 + rnd(79), at = rnd((unsigned)n + 1);
				for (size_t i = 0; i < k; i++) ins[i] = rnd(d) ? 'b' : '\n';
				if (text_insert(t, at, ins, k)) exit(2);
				memmove(m + at + k, m + at, n - at); memcpy(m + at, ins, k); n += k;
			} else if (n) {
				size_t at = rnd((unsigned)n), k = rnd((unsigned)(n - at < 90 ? n - at : 90) + 1);
				if (text_delete(t, at, k)) exit(2);
				memmove(m + at, m + at + k, n - at - k); n -= k;
			}
			if (e % 10 == 9) all(t, n);
		}
		text_free(t);
	}
	printf("edges: %llu line starts agree with the model\n", checks);
	return 0;
}
