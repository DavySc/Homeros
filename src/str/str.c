/* Thread 0's, agreed: the one growable string git and lsp share (str.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"

int str_add(Str *s, const char *p, size_t n)
{
	if (s->n + n + 1 > s->cap) {
		size_t c = s->cap ? s->cap : 256;
		while (c < s->n + n + 1) c *= 2;
		char *q = realloc(s->s, c);
		if (!q) return -1;
		s->s = q; s->cap = c;
	}
	if (n) memcpy(s->s + s->n, p, n);
	s->n += n;
	s->s[s->n] = 0;
	return 0;
}

int str_addz(Str *s, const char *p) { return str_add(s, p, strlen(p)); }
void str_free(Str *s) { free(s->s); *s = (Str){ 0 }; }

int str_addf(Str *s, const char *fmt, ...)
{
	char b[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	return n < 0 || (size_t)n >= sizeof b ? -1 : str_add(s, b, (size_t)n);
}
