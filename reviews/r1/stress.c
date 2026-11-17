/* Deep trees: random edits on 0 to 48 MB (height up to 4), shape and bytes
   against an array.  stress SEED OPS */
#include "shape.h"
enum { MAX = 48 << 20 };
static char *m, *b, *s;
static size_t mn;
static uint64_t r;
static uint64_t nx(void) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; }
static void rfill(char *p, size_t n) { for (size_t j = 0; j < n; j++) { uint64_t v = nx(); p[j] = v % 23 < 2 ? '\n' : (char)('a' + v % 26); } }
int main(int argc __attribute__((unused)), char **argv)
{
	r = 88172645463325252ull + strtoull(argv[1], 0, 10) * 2654435761ull;
	long ops = atol(argv[2]);
	m = malloc(MAX); b = malloc(MAX); s = malloc(MAX);
	mn = 20 << 20; rfill(m, mn);
	Text *t = text_new(m, mn);
	int deep = shape(t), fails = 0;
	for (long i = 0; i < ops; i++) {
		size_t pos = nx() % (mn + 1), n;
		unsigned kind = nx() % 100;
		n = kind < 80 ? nx() % 40 : kind < 95 ? nx() % 5000 : kind < 99 ? nx() % (1 << 20) : nx() % (24 << 20);
		if (nx() % 400 == 0) { pos = nx() % 3 ? 0 : pos; n = mn - pos - (nx() % 3 ? 0 : nx() % (mn - pos + 1)); goto del; }
		if (nx() & 1 && mn + n <= MAX) {
			rfill(s, n < 70000 ? n : 70000); for (size_t j = 70000; j < n; j++) s[j] = s[j - 69997];
			if (nx() % 50 == 0) {           /* out of memory somewhere inside */
				fail_in = nx() % 40;
				int rc = text_insert(t, pos, s, n);
				fail_in = -1;
				if (rc) { fails++; goto check; }
			} else if (text_insert(t, pos, s, n)) abort();
			memmove(m + pos + n, m + pos, mn - pos); memcpy(m + pos, s, n); mn += n;
		} else {
			if (n > mn - pos) n = mn - pos;
del:
			if (text_delete(t, pos, n)) abort();
			memmove(m + pos, m + pos + n, mn - pos - n); mn -= n;
		}
check:;
		size_t a = pos > 3000 ? pos - 3000 : 0, want = mn - a < 6000 ? mn - a : 6000;
		if (text_len(t) != mn || text_read(t, a, b, 6000) != want || memcmp(b, m + a, want)) { fprintf(stderr, "near differs at op %ld\n", i); abort(); }
		if (i % 16 == 0 || n > 100000) {
			int h = shape(t); if (h > deep) deep = h;
			if (text_read(t, 0, b, MAX) != mn || memcmp(b, m, mn)) { fprintf(stderr, "bytes differ at op %ld\n", i); abort(); }
			size_t l = 0, q = nx() % (mn + 1);
			for (size_t j = 0; j < q; j++) l += m[j] == '\n';
			if (text_line_of(t, q) != l) abort();
			size_t ls = text_line_start(t, l);
			if (ls > q || (l && m[ls - 1] != '\n') || memchr(m + ls, '\n', q - ls)) abort();
		}
	}
	shape(t);
	if (text_delete(t, 0, mn)) abort();
	if (shape(t) != 0 || text_len(t) || text_lines(t)) abort();
	text_free(t);
	printf("ok seed %s ops %ld deepest %d oom-fails %d\n", argv[1], ops, deep, fails);
	return 0;
}
