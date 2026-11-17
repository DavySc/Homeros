/* The rope's own shape, which text.h cannot see: this file compiles
   src/text/text.c a second time under other names, with malloc that can be
   made to fail.  Checks every B-tree invariant and the bytes against an
   array model, on texts of megabytes (test_text's 64 KB is one level). */
#include <string.h>
#include <time.h>
#include "check.h"
#define text_new r_new
#define text_free r_free
#define text_len r_len
#define text_insert r_insert
#define text_delete r_delete
#define text_read r_read
#define text_lines r_lines
#define text_line_start r_line_start
#define text_line_of r_line_of
static long fail_in = -1;      /* malloc fails when this reaches 0 */
static void *try_malloc(size_t n) { return fail_in-- == 0 ? NULL : malloc(n); }
static void *try_calloc(size_t m, size_t n) { return fail_in-- == 0 ? NULL : calloc(m, n); }
#define malloc try_malloc
#define calloc try_calloc
#include "../src/text/text.c"
#undef malloc
#undef calloc

/* every node but the root at least half full, an inner root with two
   children or more, every count right (all leaves are at depth h by
   construction: a node's height is its distance from the root) */
static void shape(const Kid *k, int h, int root)
{
	const Node *x = k->p;
	size_t c = h ? K : B;
	CHECK(x->n <= c);
	CHECK(root ? (!h || x->n >= 2) : x->n >= c / 2);
	Kid s = { k->p, 0, 0 };
	sum(&s, h);
	CHECK(s.len == k->len && s.nl == k->nl);
	if (h) for (size_t i = 0; i < x->n; i++) shape(&x->k[i], h - 1, 0);
}

static char *model, *buf;
static size_t mn;
enum { MAX = 6 << 20 };

static int cmp(const void *a, const void *b)
{
	size_t x = *(const size_t *)a, y = *(const size_t *)b;
	return (x > y) - (x < y);
}

/* the whole text against the model: shape, bytes, and the line lookups at
   40 random places (one pass over the model, so it runs every few hundred edits) */
static void same(const Text *t)
{
	shape(&t->r, t->h, 1);
	CHECK(t->ns <= SPARE);
	CHECK(r_len(t) == mn);
	CHECK(r_read(t, 0, buf, MAX) == mn && !memcmp(buf, model, mn));
	size_t p[40], l = 0, i = 0;
	for (int j = 0; j < 40; j++) p[j] = rnd((unsigned)mn + 2);
	qsort(p, 40, sizeof *p, cmp);
	for (int j = 0; j < 40; j++) {
		for (; i < p[j] && i < mn; i++) l += model[i] == '\n';
		size_t n = rnd(3000), want = p[j] < mn ? (n < mn - p[j] ? n : mn - p[j]) : 0;
		CHECK(r_read(t, p[j], buf, n) == want && !memcmp(buf, model + p[j], want));
		CHECK(r_line_of(t, p[j]) == l);
		size_t s = r_line_start(t, l);
		CHECK(l ? s > 0 && s <= p[j] && model[s - 1] == '\n' && r_line_of(t, s) == l : s == 0);
		CHECK(r_line_start(t, l + 1) == (memchr(model + s, '\n', mn - s) ? (size_t)((char *)memchr(model + s, '\n', mn - s) - model) + 1 : mn));
	}
	for (; i < mn; i++) l += model[i] == '\n';
	CHECK(r_lines(t) == l && r_line_start(t, l + 1) == mn);
}

/* after each edit: the bytes around it */
static void near(const Text *t, size_t pos)
{
	size_t a = pos > 2000 ? pos - 2000 : 0, want = mn - a < 4000 ? mn - a : 4000;
	CHECK(r_len(t) == mn && r_read(t, a, buf, 4000) == want && !memcmp(buf, model + a, want));
}

static void fill(char *s, size_t n)
{
	for (size_t j = 0; j < n; j++) s[j] = rnd(40) ? "abcdefgh"[rnd(8)] : '\n';
}

/* CPU seconds to look up every line start of n bytes with a '\n' every
   `every` bytes */
static double lookups(size_t n, size_t every)
{
	for (size_t i = 0; i < n; i++) model[i] = i % every == every - 1 ? '\n' : 'a';
	Text *t = r_new(model, n);
	CHECK(t);
	size_t lines = r_lines(t), sink = 0;
	clock_t c = clock();
	for (int rep = 0; rep < 5; rep++)
		for (size_t l = 0; l <= lines; l++) sink += r_line_start(t, l);
	c = clock() - c;
	CHECK(sink);
	r_free(t);
	return (double)c / CLOCKS_PER_SEC / (5 * ((double)lines + 1));
}

int main(void)
{
	model = malloc(MAX); buf = malloc(MAX);
	CHECK(model && buf);
	mn = 1 << 20;
	fill(model, mn);
	Text *t = r_new(model, mn);
	CHECK(t && t->h >= 2);
	same(t);
	int deepest = 0;
	for (int i = 0; i < 20000; i++) {
		size_t pos = rnd((unsigned)mn + 1), big = !rnd(100), n = big ? rnd(1 << 18) : rnd(12);
		if (rnd(2) && mn + n <= MAX) {
			char *s = buf;
			fill(s, n);
			CHECK(!r_insert(t, pos, s, n));
			memmove(model + pos + n, model + pos, mn - pos); memcpy(model + pos, s, n); mn += n;
		} else {
			if (n > mn - pos) n = mn - pos;
			CHECK(!r_delete(t, pos, n));
			memmove(model + pos, model + pos + n, mn - pos - n); mn -= n;
		}
		if (t->h > deepest) deepest = t->h;
		near(t, pos);
		if (i % 200 == 0) same(t);
	}
	CHECK(deepest >= 3);
	/* out of memory at every allocation of a large insert: text unchanged */
	size_t n = 300000, pos = mn / 3;
	fill(buf, n);
	int fails = 0;
	for (long f = 0; ; f++) {
		fail_in = f;
		int rc = r_insert(t, pos, buf, n);
		fail_in = -1;
		if (!rc) { CHECK(!r_delete(t, pos, n)); break; }
		fails++;
		same(t);
	}
	CHECK(fails > 0);
	for (long f = 0; f < 3; f++) {     /* the Text, its root, its first spare */
		fail_in = f;
		CHECK(!r_new(model, 100));
	}
	fail_in = -1;
	/* everything deleted, then the empty text */
	CHECK(!r_delete(t, 0, mn)); mn = 0;
	same(t);
	CHECK(t->h == 0);
	r_free(t);
	/* A line lookup costs about the same in a leaf of empty lines as in one of
	   50-byte lines; one memchr per '\n' made it 37 times dearer (measured). */
	double dense = lookups(200000, 1), sparse = lookups(5000000, 50);
	CHECK(dense < 4 * sparse);
	printf("test_text_rope: line lookup %.0f ns among empty lines, %.0f ns among 50-byte lines\n", dense * 1e9, sparse * 1e9);
	printf("test_text_rope: 20000 edits on 1 to 6 MB, height up to %d, invariants hold; %d failed allocations leave the text unchanged\n", deepest, fails);
	return 0;
}
