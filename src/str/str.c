/* Thread 0's, agreed: the one growable string git and lsp share (str.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"

/* Room for n more bytes and the NUL. */
static int grow(Str *s, size_t n)
{
	if (s->n + n + 1 > s->cap) {
		size_t c = s->cap ? s->cap : 256;
		while (c < s->n + n + 1) c *= 2;
		char *q = realloc(s->s, c);
		if (!q) return -1;
		s->s = q; s->cap = c;
	}
	return 0;
}

int str_add(Str *s, const char *p, size_t n)
{
	if (grow(s, n)) return -1;
	if (n) memcpy(s->s + s->n, p, n);
	s->n += n;
	s->s[s->n] = 0;
	return 0;
}

int str_addz(Str *s, const char *p) { return str_add(s, p, strlen(p)); }
void str_free(Str *s) { free(s->s); *s = (Str){ 0 }; }

/* Formatted into the room there is, then grown and formatted again only
   when it did not fit: any length (a URI of a long path once lost its
   didOpen to a fixed buffer, PR 27 review), one pass when it fits (package
   22's 100,000 rows of 12 columns: 219 ms with two passes, 151 with one,
   8 alternating runs each, measured) */
int str_addf(Str *s, const char *fmt, ...)
{
	va_list ap;
	size_t room = s->cap - s->n;
	va_start(ap, fmt);
	int n = vsnprintf(s->cap ? s->s + s->n : NULL, room, fmt, ap);
	va_end(ap);
	if (n < 0 || ((size_t)n >= room && grow(s, (size_t)n))) {
		if (s->cap) s->s[s->n] = 0;   /* the cut text the first pass left */
		return -1;
	}
	if ((size_t)n >= room) {
		va_start(ap, fmt);
		vsnprintf(s->s + s->n, (size_t)n + 1, fmt, ap);
		va_end(ap);
	}
	s->n += (size_t)n;
	return 0;
}

int str_lines(Str *part, const char *b, size_t n, void (*fn)(void *ctx, char *line, size_t n), void *ctx)
{
	for (const char *e = b + n, *nl; b < e; b = nl + 1) {
		if (!(nl = memchr(b, '\n', (size_t)(e - b)))) return str_add(part, b, (size_t)(e - b));
		if (str_add(part, b, (size_t)(nl - b))) return -1;
		size_t k = part->n;
		part->n = 0;
		fn(ctx, part->s, k);
	}
	return 0;
}
