/* The rope's own shape and its out-of-memory path, which text.h cannot see:
   src/text/text.c is compiled in here under other names, with a malloc that
   fails on demand.  Content is checked against a plain array on every edit. */
#include <stdlib.h>
#include <string.h>
#include "check.h"

static long budget = -1;   /* mallocs left before one fails; -1: never */
static void *tmalloc(size_t n) { return budget-- == 0 ? NULL : malloc(n); }

#define malloc tmalloc
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
#undef malloc

/* Every leaf at one depth, every node but the root at least half full, each
   entry's counts equal to its subtree's; returns the subtree's leaf depth. */
static int shape(const Node *a, const Ent *e, int root)
{
	Ent c;
	count(a, &c);
	CHECK(c.len == e->len && c.nl == e->nl);
	CHECK(a->n <= cap(a) && (root || a->n >= cap(a) / 2));
	if (a->leaf) return 0;
	CHECK(a->n >= 2 || !root);
	int d = shape(a->e[0].k, &a->e[0], 0);
	for (size_t i = 1; i < a->n; i++) CHECK(shape(a->e[i].k, &a->e[i], 0) == d);
	return d + 1;
}

static char model[1 << 21], buf[1 << 21];
static size_t mn;

static void same(Text *t)
{
	Ent e = { t->root, t->len, t->nl };
	shape(t->root, &e, 1);
	CHECK(r_len(t) == mn && r_read(t, 0, buf, sizeof buf) == mn && !memcmp(buf, model, mn));
}

static void tins(Text *t, size_t pos, const char *s, size_t n)
{
	CHECK(!r_insert(t, pos, s, n));
	memmove(model + pos + n, model + pos, mn - pos); memcpy(model + pos, s, n); mn += n;
}

static void tdel(Text *t, size_t pos, size_t n)
{
	CHECK(!r_delete(t, pos, n));
	memmove(model + pos, model + pos + n, mn - pos - n); mn -= n;
}

int main(void)
{
	static char src[1 << 20];
	for (size_t i = 0; i < sizeof src; i++) src[i] = rnd(20) ? (char)('a' + rnd(26)) : '\n';

	/* Random edits of every size, from a byte to a megabyte, mixed so the tree
	   grows, shrinks back to a leaf and grows again. */
	Text *t = r_new(src, 300000);
	memcpy(model, src, 300000); mn = 300000;
	same(t);
	for (int i = 0; i < 3000; i++) {
		size_t pos = rnd((unsigned)mn + 1), big = rnd(10) == 0, n = big ? rnd(1 << 18) : rnd(3000);
		if (rnd(2) && mn + n <= sizeof model) tins(t, pos, src + rnd(1000), n);
		else tdel(t, pos, n < mn - pos ? n : mn - pos);
		if (rnd(200) == 0) tdel(t, 0, mn);   /* all of it */
		same(t);
	}
	r_free(t);

	/* Out of memory on the k-th malloc of an insert: -1 and the text as it
	   was, for every k up to one past the last malloc the insert makes. */
	for (int fail = 0, done = 0; !done; fail++) {
		t = r_new(src, 100000);
		memcpy(model, src, 100000); mn = 100000;
		budget = fail;
		int r = r_insert(t, 5000, src, 200000);
		budget = -1;
		if (r) CHECK(r == -1);
		else { memmove(model + 205000, model + 5000, 95000); memcpy(model + 5000, src, 200000); mn += 200000; done = 1; }
		same(t);
		tins(t, 7, "x\n", 2);   /* still usable */
		same(t);
		r_free(t);
	}
	budget = 0;
	CHECK(!r_new("ab", 2));
	budget = 1;
	CHECK(!r_new("ab", 2));   /* the spares fail after the root is made */
	budget = -1;
	puts("test_text_rope: shape and content hold over 3000 edits up to 256 KB; out of memory leaves the text unchanged");
	return 0;
}
