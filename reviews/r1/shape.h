/* Reviewer's harness: includes a build's text.c under other names and checks
   the B-tree's shape.  -DBUILD_A or -DBUILD_B picks the node layout. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long fail_in = -1;
static void *try_malloc(size_t n) { return fail_in-- == 0 ? NULL : malloc(n); }
static void *try_calloc(size_t m, size_t n) { return fail_in-- == 0 ? NULL : calloc(m, n); }
#define malloc try_malloc
#define calloc try_calloc
#include TEXT_C
#undef malloc
#undef calloc
#define DIE(msg) do { fprintf(stderr, "SHAPE: %s line %d\n", msg, __LINE__); abort(); } while (0)
#ifdef BUILD_A
static void shape_a(const Kid *k, int h, int root)
{
	const Node *x = k->p;
	size_t c = h ? K : B;
	if (x->n > c) DIE("over");
	if (root ? h && x->n < 2 : x->n < c / 2) DIE("thin");
	Kid s = { k->p, 0, 0 };
	sum(&s, h);
	if (s.len != k->len || s.nl != k->nl) DIE("count");
	if (h) for (size_t i = 0; i < x->n; i++) shape_a(&x->k[i], h - 1, 0);
}
static int shape(const Text *t) { shape_a(&t->r, t->h, 1); if (t->ns > SPARE) DIE("spare"); return t->h; }
#else
static int shape_b(const Node *a, const Ent *e, int root)
{
	Ent c;
	count(a, &c);
	if (c.len != e->len || c.nl != e->nl) DIE("count");
	if (a->n > cap(a)) DIE("over");
	if (!root && a->n < cap(a) / 2) DIE("thin");
	if (a->leaf) return 0;
	if (root && a->n < 2) DIE("root");
	int d = shape_b(a->e[0].k, &a->e[0], 0);
	for (size_t i = 1; i < a->n; i++) if (shape_b(a->e[i].k, &a->e[i], 0) != d) DIE("depth");
	return d + 1;
}
static int shape(const Text *t) { Ent e = { t->root, t->len, t->nl }; return shape_b(t->root, &e, 1); }
#endif
