/* Out of memory at every allocation of inserts of several sizes and places,
   and of text_new: -1/NULL, bytes unchanged, shape kept, still usable. */
#include "shape.h"
static char m[8 << 20], b[8 << 20], s[4 << 20];
int main(void)
{
	for (size_t i = 0; i < sizeof s; i++) s[i] = i % 37 == 5 ? '\n' : (char)('a' + i % 26);
	size_t sizes[] = { 0, 1, 507, 508, 509, 1007, 1008, 1009, 1015, 1016, 1017, 2032, 5000, 100000, 1000000 };
	size_t base[] = { 0, 1, 1016, 50000, 300000 };
	long total = 0;
	for (size_t bi = 0; bi < 5; bi++) for (size_t si = 0; si < 15; si++) for (int where = 0; where < 3; where++) {
		size_t mn = base[bi], n = sizes[si], pos = where == 0 ? 0 : where == 1 ? mn / 2 : mn;
		for (long f = 0; ; f++) {
			Text *t = text_new(s, mn);
			if (!t) abort();
			memcpy(m, s, mn);
			fail_in = f;
			int rc = text_insert(t, pos, s + 3, n);
			fail_in = -1;
			if (!rc) { memmove(m + pos + n, m + pos, mn - pos); memcpy(m + pos, s + 3, n); }
			size_t len = mn + (rc ? 0 : n);
			shape(t);
			if (text_len(t) != len || text_read(t, 0, b, sizeof b) != len || memcmp(b, m, len)) { fprintf(stderr, "bytes changed\n"); abort(); }
			if (text_insert(t, len / 3, "x\n", 2) || text_delete(t, len / 3, 2)) abort();
			shape(t);
			text_free(t);
			if (!rc) break;
			total++;
		}
	}
	for (size_t n = 0; n < 6000; n += 127) for (long f = 0; ; f++) {
		fail_in = f;
		Text *t = text_new(s, n);
		fail_in = -1;
		if (t) { shape(t); if (text_read(t, 0, b, sizeof b) != n || memcmp(b, s, n)) abort(); text_free(t); break; }
		total++;
	}
	printf("oom ok: %ld failed allocations\n", total);
	return 0;
}
