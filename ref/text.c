/* Reference text: one array.  O(n) per edit, which is why package 1 exists;
   kept as the oracle that package's fuzzer compares against. */
#include <stdlib.h>
#include <string.h>
#include "text.h"

struct Text { char *b; size_t n, cap; };

static int reserve(Text *t, size_t n)
{
	if (n <= t->cap) return 0;
	size_t cap = t->cap ? t->cap : 64;
	while (cap < n) cap *= 2;
	char *b = realloc(t->b, cap);
	if (!b) return -1;
	t->b = b; t->cap = cap;
	return 0;
}

Text *text_new(const char *s, size_t n)
{
	Text *t = calloc(1, sizeof *t);
	if (!t || reserve(t, n)) { free(t); return NULL; }
	if (n) memcpy(t->b, s, n);
	t->n = n;
	return t;
}

void text_free(Text *t) { if (t) free(t->b); free(t); }
size_t text_len(const Text *t) { return t->n; }

int text_insert(Text *t, size_t pos, const char *s, size_t n)
{
	if (pos > t->n || reserve(t, t->n + n)) return -1;
	if (!n) return 0;               /* t->b may still be NULL */
	memmove(t->b + pos + n, t->b + pos, t->n - pos);
	memcpy(t->b + pos, s, n);
	t->n += n;
	return 0;
}

int text_delete(Text *t, size_t pos, size_t n)
{
	if (pos > t->n || n > t->n - pos) return -1;
	if (!n) return 0;
	memmove(t->b + pos, t->b + pos + n, t->n - pos - n);
	t->n -= n;
	return 0;
}

size_t text_read(const Text *t, size_t pos, char *out, size_t n)
{
	if (pos >= t->n) return 0;
	if (n > t->n - pos) n = t->n - pos;
	memcpy(out, t->b + pos, n);
	return n;
}

size_t text_lines(const Text *t)
{
	size_t k = 0;
	for (size_t i = 0; i < t->n; i++) k += t->b[i] == '\n';
	return k;
}

size_t text_line_start(const Text *t, size_t l)
{
	size_t i = 0;
	for (; l && i < t->n; i++) l -= t->b[i] == '\n';
	return l ? t->n : i;
}

size_t text_line_of(const Text *t, size_t pos)
{
	size_t k = 0;
	for (size_t i = 0; i < pos && i < t->n; i++) k += t->b[i] == '\n';
	return k;
}
