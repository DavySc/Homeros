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

/* Measured first, then formatted in place: any length (a URI of a long
   path once lost its didOpen to a fixed buffer, PR 27 review). */
int str_addf(Str *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0 || grow(s, (size_t)n)) return -1;
	va_start(ap, fmt);
	vsnprintf(s->s + s->n, (size_t)n + 1, fmt, ap);
	va_end(ap);
	s->n += (size_t)n;
	return 0;
}
