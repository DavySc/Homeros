/* Text as a rope: a B+ tree whose leaves hold up to LEAF bytes and whose inner
   nodes hold up to FAN children with each child's byte and '\n' counts, the
   shape of ropey (Helix).  Every node but the root is at least half full, so
   an edit or a line lookup costs O(log n).  ref/text.c is the oracle. */
#include <stdlib.h>
#include <string.h>
#include "text.h"

typedef struct Node Node;
typedef struct { Node *k; size_t len, nl; } Ent;   /* a child and its counts */
enum { NODE = 1024, LEAF = NODE - 16, FAN = LEAF / sizeof(Ent) };
_Static_assert(2 * FAN * sizeof(Ent) <= 2 * LEAF, "the buffers of put and fill hold two inner nodes");
/* A leaf's bytes and an inner node's children share one array, so filling,
   splitting and merging are written once, in elements of size esz(a). */
struct Node {
	size_t n;           /* bytes in a leaf, children in an inner node */
	int leaf;
	union { char b[LEAF]; Ent e[FAN]; };
};
struct Text {
	Node *root, *spare;   /* spare: nodes an insert may take, linked by e[0].k */
	size_t len, nl;
};

static size_t esz(const Node *a) { return a->leaf ? 1 : sizeof(Ent); }
static size_t cap(const Node *a) { return a->leaf ? LEAF : FAN; }

/*@ axiomatic Nl {
      logic integer nl{L}(char *s, integer n) reads s[0 .. n - 1];
      axiom nl0{L}: \forall char *s; nl(s, 0) == 0;
      axiom nl1{L}: \forall char *s, integer n; n >= 0 ==>
        nl(s, n + 1) == nl(s, n) + (s[n] == '\n' ? 1 : 0);
    } */

/* Counted in blocks of 32 because a loop of fixed length is what gcc -O2
   vectorizes: with a plain byte loop the benchmark's workload took 211.2 M
   instructions instead of 54.5 M (gcc; clang 152.4 M instead of 50.0 M;
   callgrind, src/text/bench).
   Frama-C proves the result (src/text/README.md). */
/*@ requires \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures \result == nl(s, n); */
static size_t nls(const char *s, size_t n)
{
	size_t k = 0, i = 0;
	/*@ loop invariant i % 32 == 0 && i <= n && k == nl(s, i) && k <= i;
	    loop assigns i, k;
	    loop variant n - i; */
	for (; n - i >= 32; i += 32) {
		unsigned char c = 0;
		/*@ loop invariant 0 <= j <= 32 && c == nl(s, i + j) - nl(s, i) && c <= j;
		    loop assigns j, c;
		    loop variant 32 - j; */
		for (size_t j = 0; j < 32; j++) c += s[i + j] == '\n';
		k += c;
	}
	/*@ loop invariant i <= n && k == nl(s, i) && k <= i;
	    loop assigns i, k;
	    loop variant n - i; */
	for (; i < n; i++) k += s[i] == '\n';
	return k;
}

static void count(const Node *a, Ent *e)
{
	e->k = (Node *)a;
	if (a->leaf) { e->len = a->n; e->nl = nls(a->b, a->n); return; }
	e->len = e->nl = 0;
	for (size_t i = 0; i < a->n; i++) { e->len += a->e[i].len; e->nl += a->e[i].nl; }
}

static void drop(Node *a)
{
	if (!a->leaf) for (size_t i = 0; i < a->n; i++) drop(a->e[i].k);
	free(a);
}

/* Ensures the spares cover the worst insert of one chunk: a split on every
   level and a new root.  Taken up front so an insert never fails half done. */
static int stock(Text *t)
{
	size_t need = 2, have = 0;
	for (Node *a = t->root; !a->leaf; a = a->e[0].k) need++;
	for (Node *a = t->spare; a; a = a->e[0].k) have++;
	for (; have < need; have++) {
		Node *a = malloc(sizeof *a);
		if (!a) return -1;
		a->e[0].k = t->spare; t->spare = a;
	}
	return 0;
}

static Node *take(Text *t, int leaf)
{
	Node *a = t->spare;
	t->spare = a->e[0].k;
	a->leaf = leaf; a->n = 0;
	return a;
}

/* Puts the m elements in buf into a and b, half each (a gets the smaller). */
static void halve(Node *a, Node *b, const char *buf, size_t m)
{
	size_t z = esz(a), h = m / 2;
	memcpy(a->b, buf, h * z); a->n = h;
	memcpy(b->b, buf + h * z, (m - h) * z); b->n = m - h;
}

/* Inserts m elements from s at index at of a; returns the new right sibling
   when they do not fit, with the elements split evenly between the two. */
static Node *put(Text *t, Node *a, size_t at, const void *s, size_t m)
{
	size_t z = esz(a);
	if (a->n + m <= cap(a)) {
		memmove(a->b + (at + m) * z, a->b + at * z, (a->n - at) * z);
		memcpy(a->b + at * z, s, m * z);
		a->n += m;
		return NULL;
	}
	char buf[2 * LEAF];
	memcpy(buf, a->b, at * z);
	memcpy(buf + at * z, s, m * z);
	memcpy(buf + (at + m) * z, a->b + at * z, (a->n - at) * z);
	Node *b = take(t, a->leaf);
	halve(a, b, buf, a->n + m);
	return b;
}

/* Inserts n <= LEAF bytes holding c newlines at pos of the subtree a. */
static Node *ins(Text *t, Node *a, size_t pos, const char *s, size_t n, size_t c)
{
	if (a->leaf) return put(t, a, pos, s, n);
	size_t i = 0;
	while (i + 1 < a->n && pos > a->e[i].len) pos -= a->e[i++].len;
	Node *b = ins(t, a->e[i].k, pos, s, n, c);
	if (!b) { a->e[i].len += n; a->e[i].nl += c; return NULL; }
	Ent e = { b, a->e[i].len + n, a->e[i].nl + c };   /* both halves */
	count(a->e[i].k, &a->e[i]);
	e.len -= a->e[i].len; e.nl -= a->e[i].nl;
	return put(t, a, i + 1, &e, 1);
}

/* Child i of a has fewer than half its elements: merges it with a neighbour,
   or evens the two out when they do not fit in one node. */
static void fill(Node *a, size_t i)
{
	if (i + 1 == a->n) i--;
	Node *x = a->e[i].k, *y = a->e[i + 1].k;
	size_t z = esz(x);
	if (x->n + y->n <= cap(x)) {
		memcpy(x->b + x->n * z, y->b, y->n * z);
		x->n += y->n;
		a->e[i].len += a->e[i + 1].len; a->e[i].nl += a->e[i + 1].nl;
		free(y);
		a->n--;
		memmove(a->e + i + 1, a->e + i + 2, (a->n - i - 1) * sizeof(Ent));
		return;
	}
	char buf[2 * LEAF];
	memcpy(buf, x->b, x->n * z);
	memcpy(buf + x->n * z, y->b, y->n * z);
	halve(x, y, buf, x->n + y->n);
	count(x, &a->e[i]);
	count(y, &a->e[i + 1]);
}

/* Index of the child of a holding byte *p, with *p made relative to it. */
static size_t at(const Node *a, size_t *p)
{
	size_t i = 0;
	while (*p >= a->e[i].len) *p -= a->e[i++].len;
	return i;
}

/* Deletes n > 0 bytes at pos of the subtree a; returns the newlines removed. */
static size_t del(Node *a, size_t pos, size_t n)
{
	if (a->leaf) {
		size_t c = nls(a->b + pos, n);
		memmove(a->b + pos, a->b + pos + n, a->n - pos - n);
		a->n -= n;
		return c;
	}
	size_t c = 0;
	for (size_t i = at(a, &pos); n;) {
		Ent *e = &a->e[i];
		size_t m = e->len - pos < n ? e->len - pos : n, k;
		if (m == e->len) {      /* the whole child goes */
			k = e->nl;
			drop(e->k);
			a->n--;
			memmove(e, e + 1, (a->n - i) * sizeof(Ent));
		} else {
			k = del(e->k, pos, m);
			e->len -= m; e->nl -= k;
			i++;
		}
		c += k; n -= m; pos = 0;
	}
	return c;
}

static int thin(const Node *a) { return a->n < cap(a) / 2; }

/* Fills the nodes on the path to byte p that a delete left under half full.
   A child is filled before the descent, so its own children always have a
   neighbour to merge with, and again after, as merges below may thin it. */
static void mend(Node *a, size_t p)
{
	size_t q = p, i = at(a, &q);
	while (a->n > 1 && thin(a->e[i].k)) { fill(a, i); q = p; i = at(a, &q); }
	if (a->e[i].k->leaf) return;
	mend(a->e[i].k, q);
	while (a->n > 1 && thin(a->e[i].k)) { fill(a, i); q = p; i = at(a, &q); }
}

/* A root left with one child gives way to it. */
static void lift(Text *t)
{
	while (!t->root->leaf && t->root->n == 1) {
		Node *a = t->root;
		t->root = a->e[0].k;
		free(a);
	}
}

static size_t rd(const Node *a, size_t pos, char *out, size_t n)
{
	if (a->leaf) { memcpy(out, a->b + pos, n); return n; }
	size_t done = 0;
	for (size_t i = at(a, &pos); done < n; i++, pos = 0) {
		size_t m = a->e[i].len - pos < n - done ? a->e[i].len - pos : n - done;
		done += rd(a->e[i].k, pos, out + done, m);
	}
	return done;
}

Text *text_new(const char *s, size_t n)
{
	Text *t = calloc(1, sizeof *t);
	if (!t || !(t->root = malloc(sizeof *t->root))) { free(t); return NULL; }
	t->root->leaf = 1; t->root->n = 0;
	if (text_insert(t, 0, s, n)) { text_free(t); return NULL; }
	return t;
}

void text_free(Text *t)
{
	if (!t) return;
	drop(t->root);
	while (t->spare) { Node *a = t->spare; t->spare = a->e[0].k; free(a); }
	free(t);
}

size_t text_len(const Text *t) { return t->len; }
size_t text_lines(const Text *t) { return t->nl; }

int text_insert(Text *t, size_t pos, const char *s, size_t n)
{
	if (pos > t->len) return -1;
	for (size_t done = 0; done < n;) {
		size_t m = n - done < LEAF ? n - done : LEAF, c = nls(s + done, m);
		if (stock(t)) { text_delete(t, pos, done); return -1; }
		Node *b = ins(t, t->root, pos + done, s + done, m, c);
		if (b) {
			Node *r = take(t, 0);
			r->n = 2;
			count(t->root, &r->e[0]);
			count(b, &r->e[1]);
			t->root = r;
		}
		t->len += m; t->nl += c; done += m;
	}
	return 0;
}

int text_delete(Text *t, size_t pos, size_t n)
{
	if (pos > t->len || n > t->len - pos) return -1;
	if (!n) return 0;
	t->nl -= del(t->root, pos, n);
	t->len -= n;
	if (!t->root->n) t->root->leaf = 1;   /* all of it went: an empty leaf */
	/* Only the nodes holding the bytes either side of the cut were cut short. */
	for (size_t p = pos ? pos - 1 : pos; p <= pos && p < t->len; p++) {
		lift(t);
		if (!t->root->leaf) mend(t->root, p);
	}
	lift(t);
	return 0;
}

size_t text_read(const Text *t, size_t pos, char *out, size_t n)
{
	if (pos >= t->len) return 0;
	if (n > t->len - pos) n = t->len - pos;
	return n ? rd(t->root, pos, out, n) : 0;
}

size_t text_line_start(const Text *t, size_t l)
{
	if (!l) return 0;
	if (l > t->nl) return t->len;
	size_t at = 0;
	const Node *a = t->root;
	for (size_t i = 0; !a->leaf; a = a->e[i].k, i = 0)
		for (; l > a->e[i].nl; i++) { l -= a->e[i].nl; at += a->e[i].len; }
	const char *p = a->b;
	while (l--) p = (const char *)memchr(p, '\n', (size_t)(a->b + a->n - p)) + 1;
	return at + (size_t)(p - a->b);
}

size_t text_line_of(const Text *t, size_t pos)
{
	if (pos >= t->len) return t->nl;
	size_t k = 0;
	const Node *a = t->root;
	for (size_t i = 0; !a->leaf; a = a->e[i].k, i = 0)
		for (; pos >= a->e[i].len; i++) { pos -= a->e[i].len; k += a->e[i].nl; }
	return k + nls(a->b, pos);
}
