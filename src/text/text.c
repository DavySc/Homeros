/* Text as a B-tree of byte chunks, the shape of ropey (DESIGN.md, Text): every
   leaf at the same depth, each node (but the root) at least half full, each
   parent holding its children's byte and '\n' counts, so an edit or a lookup
   walks one root-to-leaf path.  Unlike ropey the counts are bytes, not chars:
   text.h asks for nothing else. */
#include <stdlib.h>
#include <string.h>
#include "text.h"

typedef struct Node Node;
typedef struct { Node *p; size_t len, nl; } Kid;   /* a child and its counts */
/* B makes a node 1 KB, ropey's leaf size; a leaf holds B bytes, an inner node K kids */
enum { B = 1024 - sizeof(size_t), K = B / sizeof(Kid) };
struct Node { size_t n; union { char b[B]; Kid k[K]; }; };
/* Height stays below 14: with nodes half full, 21^13 leaves of 508 bytes pass SIZE_MAX. */
enum { SPARE = 16 };
struct Text {
	Kid r;                 /* the root, h levels above the leaves */
	int h, ns;
	Node *spare[SPARE];    /* taken before an insert, so a split cannot fail halfway */
};

/* Counts in blocks of 32 so that gcc and clang -O2 compile the inner loop to
   vector compares (the plain loop stays one byte a step); a block's count fits
   in a byte. */
static size_t nls(const char *s, size_t n)
{
	size_t c = 0, i = 0;
	for (; i + 32 <= n; i += 32) {
		unsigned char k = 0;
		for (int j = 0; j < 32; j++) k += s[i + j] == '\n';
		c += k;
	}
	for (; i < n; i++) c += s[i] == '\n';
	return c;
}

static void sum(Kid *k, int h)
{
	const Node *x = k->p;
	if (!h) { k->len = x->n; k->nl = nls(x->b, x->n); return; }
	k->len = k->nl = 0;
	for (size_t i = 0; i < x->n; i++) { k->len += x->k[i].len; k->nl += x->k[i].nl; }
}

/* m items of size z from tmp, split between x and y with both at least half full */
static void spread(Node *x, Node *y, const char *tmp, size_t m, size_t z)
{
	x->n = m / 2; y->n = m - x->n;
	memcpy(x->b, tmp, x->n * z);
	memcpy(y->b, tmp + x->n * z, y->n * z);
}

/* Puts s[0..n), n <= B, at pos under k (height h).  When k's node splits,
   returns 1 with the new right half in *r. */
static int ins(Text *t, Kid *k, int h, size_t pos, const char *s, size_t n, size_t nl, Kid *r)
{
	Node *x = k->p;
	size_t at = pos, m = n, z = h ? sizeof(Kid) : 1, c = h ? K : B;
	Kid kr;
	const char *it = s;
	k->len += n; k->nl += nl;
	if (h) {
		size_t i = 0;
		while (pos > x->k[i].len) pos -= x->k[i++].len;
		if (!ins(t, &x->k[i], h - 1, pos, s, n, nl, &kr)) return 0;
		at = i + 1; m = 1; it = (const char *)&kr;
	}
	if (x->n + m <= c) {
		memmove(x->b + (at + m) * z, x->b + at * z, (x->n - at) * z);
		memcpy(x->b + at * z, it, m * z);
		x->n += m;
		return 0;
	}
	char tmp[2 * B];
	memcpy(tmp, x->b, at * z);
	memcpy(tmp + at * z, it, m * z);
	memcpy(tmp + (at + m) * z, x->b + at * z, (x->n - at) * z);
	r->p = t->spare[--t->ns];
	spread(x, r->p, tmp, x->n + m, z);
	sum(r, h);
	k->len -= r->len; k->nl -= r->nl;
	return 1;
}

/* Child i of x, at height h, fell below half: join it with a neighbour, or
   share the two evenly when they do not fit in one node (B-tree deletion). */
static void join(Node *x, size_t i, int h)
{
	size_t a = i + 1 < x->n ? i : i - 1, z = h ? sizeof(Kid) : 1, c = h ? K : B;
	Kid *p = &x->k[a], *q = p + 1;
	Node *P = p->p, *Q = q->p;
	size_t len = p->len + q->len, nl = p->nl + q->nl;
	if (P->n + Q->n <= c) {
		memcpy(P->b + P->n * z, Q->b, Q->n * z);
		P->n += Q->n;
		free(Q);
		p->len = len; p->nl = nl;
		memmove(q, q + 1, (x->n - a - 2) * sizeof *q);
		x->n--;
		return;
	}
	char tmp[2 * B];
	memcpy(tmp, P->b, P->n * z);
	memcpy(tmp + P->n * z, Q->b, Q->n * z);
	spread(P, Q, tmp, P->n + Q->n, z);
	sum(p, h);
	q->len = len - p->len; q->nl = nl - p->nl;
}

/* Deletes up to n bytes at pos < k->len, stopping at the end of a leaf;
   returns how many it deleted. */
static size_t del(Kid *k, int h, size_t pos, size_t n)
{
	Node *x = k->p;
	if (!h) {
		if (n > x->n - pos) n = x->n - pos;
		k->nl -= nls(x->b + pos, n);
		memmove(x->b + pos, x->b + pos + n, x->n - pos - n);
		x->n -= n; k->len -= n;
		return n;
	}
	size_t i = 0;
	while (pos >= x->k[i].len) pos -= x->k[i++].len;
	size_t nl = x->k[i].nl;
	n = del(&x->k[i], h - 1, pos, n);
	k->len -= n; k->nl -= nl - x->k[i].nl;
	if (x->k[i].p->n < (h > 1 ? K : B) / 2) join(x, i, h - 1);
	return n;
}

/* The leaf holding byte pos < len; leaves pos's offset in it and adds the
   '\n' before the leaf to *nl. */
static const Node *find(const Text *t, size_t *pos, size_t *nl)
{
	const Node *x = t->r.p;
	for (int h = t->h; h; h--) {
		const Kid *k = x->k;
		while (*pos >= k->len) { *pos -= k->len; *nl += k->nl; k++; }
		x = k->p;
	}
	return x;
}

Text *text_new(const char *s, size_t n)
{
	Text *t = calloc(1, sizeof *t);
	if (!t || !(t->r.p = calloc(1, sizeof *t->r.p)) || text_insert(t, 0, s, n)) {
		text_free(t);
		return NULL;
	}
	return t;
}

static void drop(Node *x, int h)
{
	if (h) for (size_t i = 0; i < x->n; i++) drop(x->k[i].p, h - 1);
	free(x);
}

void text_free(Text *t)
{
	if (!t) return;
	if (t->r.p) drop(t->r.p, t->h);
	while (t->ns) free(t->spare[--t->ns]);
	free(t);
}

size_t text_len(const Text *t) { return t->r.len; }
size_t text_lines(const Text *t) { return t->r.nl; }

int text_insert(Text *t, size_t pos, const char *s, size_t n)
{
	if (pos > t->r.len) return -1;
	for (size_t done = 0, m; done < n; done += m) {
		/* a split takes one node per level and a new root one more */
		while (t->ns < t->h + 2) {
			Node *x = malloc(sizeof *x);
			if (!x) { text_delete(t, pos, done); return -1; }
			t->spare[t->ns++] = x;
		}
		m = n - done < B ? n - done : B;
		Kid r;
		if (ins(t, &t->r, t->h, pos + done, s + done, m, nls(s + done, m), &r)) {
			Node *x = t->spare[--t->ns];
			x->n = 2; x->k[0] = t->r; x->k[1] = r;
			t->r.p = x; t->r.len += r.len; t->r.nl += r.nl;
			t->h++;
		}
	}
	return 0;
}

int text_delete(Text *t, size_t pos, size_t n)
{
	if (pos > t->r.len || n > t->r.len - pos) return -1;
	while (n) {
		n -= del(&t->r, t->h, pos, n);
		if (t->h && t->r.p->n == 1) {   /* a root with one child is one level too many */
			Node *x = t->r.p;
			t->r.p = x->k[0].p;
			t->h--;
			free(x);
		}
	}
	return 0;
}

size_t text_read(const Text *t, size_t pos, char *out, size_t n)
{
	size_t got = 0, nl = 0;
	while (got < n && pos < t->r.len) {
		size_t o = pos;
		const Node *x = find(t, &o, &nl);
		size_t m = x->n - o < n - got ? x->n - o : n - got;
		memcpy(out + got, x->b + o, m);
		got += m; pos += m;
	}
	return got;
}

size_t text_line_start(const Text *t, size_t l)
{
	if (!l) return 0;
	if (l > t->r.nl) return t->r.len;
	const Node *x = t->r.p;
	size_t pos = 0;
	for (int h = t->h; h; h--) {
		const Kid *k = x->k;
		while (l > k->nl) { l -= k->nl; pos += k->len; k++; }
		x = k->p;
	}
	const char *p = x->b;
	while (l--) p = (const char *)memchr(p, '\n', (size_t)(x->b + x->n - p)) + 1;   /* the leaf holds it */
	return pos + (size_t)(p - x->b);
}

size_t text_line_of(const Text *t, size_t pos)
{
	if (pos >= t->r.len) return t->r.nl;
	size_t nl = 0;
	const Node *x = find(t, &pos, &nl);
	return nl + nls(x->b, pos);
}
