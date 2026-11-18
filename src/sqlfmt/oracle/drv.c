/* src/sqlfmt/oracle: formats a file (argv[1]) as Homeros does and prints it */
#include <stdio.h>
#include <stdlib.h>
#include "text.h"
#include "../src/sqlfmt/sqlfmt.h"
int main(int argc, char **argv)
{
	static char buf[1 << 24];
	FILE *f = argc > 1 ? fopen(argv[1], "rb") : stdin;
	size_t n = fread(buf, 1, sizeof buf, f);
	Text *t = text_new(buf, n);
	size_t a = 0, z = n, m; const char *r;
	int k = sqlfmt(t, &a, &z, &r, &m);
	if (k < 0) return 2;
	fwrite(buf, 1, a, stdout); fwrite(r, 1, m, stdout); fwrite(buf + z, 1, n - z, stdout);
	if (k) fprintf(stderr, "kept %d\n", k);
	text_free(t);
	return 0;
}
