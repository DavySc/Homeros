/* PR 175 review probe: sql_vars on stdin.
   vars            : one batch on stdin; prints "vars: @a @b" and each DECLARE default
   vars -m         : many batches, each "#batch N\n<bytes>\n"; prints one "#vars ..." line per batch */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sql/run.h"

static void one(const char *s, size_t n, int many)
{
	Str got = {0}, d = {0};
	Text *x = text_new(s, n);
	if (!x || sql_vars(x, 0, n, &got, &d)) { puts("oom"); exit(2); }
	printf(many ? "#vars" : "vars:");
	for (size_t i = 0; i < got.n; i += strlen(got.s + i) + 1) printf(" %s", got.s + i);
	printf("\n");
	if (!many) for (size_t i = 0; i < d.n;) {
		const char *nm = d.s + i, *v = nm + strlen(nm) + 1;
		printf("default %s = [%s]\n", nm, v);
		i = (size_t)(v - d.s) + strlen(v) + 1;
	}
	text_free(x), free(got.s), free(d.s);
}

int main(int argc, char **argv)
{
	static char s[1 << 22];
	if (argc > 1 && !strcmp(argv[1], "-m")) {
		size_t n;
		while (scanf("#batch %zu", &n) == 1) {
			getchar();
			if (n >= sizeof s || fread(s, 1, n, stdin) != n) return 2;
			getchar();
			one(s, n, 1);
		}
		return 0;
	}
	size_t n = fread(s, 1, sizeof s, stdin);
	one(s, n, 0);
	return 0;
}
