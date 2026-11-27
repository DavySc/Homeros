#include <stdio.h>
#include <stdlib.h>
#include "text.h"
#include "tsql.h"
#include "lean.h"
int main(int argc, char **argv)
{
	(void)argc;
	FILE *f = fopen(argv[1], "rb"); static char b[1 << 20]; size_t n = fread(b, 1, sizeof b, f);
	Text *t = text_new(b, n);
	static TsqlRd r; r.t = t;
	size_t ntok = 0, max = 0, maxat = 0;
	for (size_t p = 0, e; p < n; p = e) { e = lean_lex(&r, p, 0, 0, NULL); if (e - p > max) max = e - p, maxat = p; ntok++; }
	printf("%zu bytes, %zu tokens, longest %zu bytes at line %zu\n", n, ntok, max, text_line_of(t, maxat) + 1);
	return 0;
}
