/* PR 127's checkpoint check alone, many times: the walk to the last of
   100,000 lines, then lines shown going up (test_org.c's text and calls).
   argv[1]: runs; argv[2]: 0 = the PR's lines (n / 5 * j), 1 = lines at
   checkpoints (4096 * j).  Prints walk ns, going-up ns per run. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
int main(int argc, char **argv)
{
	int runs = argc > 1 ? atoi(argv[1]) : 100, at = argc > 2 ? atoi(argv[2]) : 0;
	const char *ln = "a *b* x^{2}\n";
	size_t n = 100000, w = strlen(ln), m = 0;
	char *many = malloc(n * w);
	while (m < n * w) memcpy(many + m, ln, w), m += w;
	for (int r = 0; r < runs; r++) {
		Text *t = text_new(many, m);
		OrgFx *x = orgfx_new(t);
		if (!orgfx_update(x, NULL)) return 1;
		struct timespec u[3];
		Conceal c[64];
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &u[0]);
		orgfx_conceal(x, n - 1, c, 64);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &u[1]);
		for (size_t j = 4; j > 0; j--) orgfx_conceal(x, at ? 4096 * (5 * j - 1) : n / 5 * j, c, 64);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &u[2]);
		long d[2];
		for (int i = 0; i < 2; i++) d[i] = (u[i + 1].tv_sec - u[i].tv_sec) * 1000000000 + (u[i + 1].tv_nsec - u[i].tv_nsec);
		printf("%ld %ld\n", d[0], d[1]);
		orgfx_free(x);
		text_free(t);
	}
	free(many);
	return 0;
}
