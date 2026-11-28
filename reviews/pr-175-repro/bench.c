/* PR 175 review: sql_vars' time over a script.  bench FILE CALLS: ms per call, 5 runs */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/sql/run.h"
int main(int argc, char **argv)
{
	static char s[1 << 23];
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(s, 1, sizeof s, f);
	int calls = argc > 2 ? atoi(argv[2]) : 9;
	Text *x = text_new(s, n);
	Str got = {0}, d = {0};
	for (int r = 0; r < 5; r++) {
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (int i = 0; i < calls; i++) d.n = 0, sql_vars(x, 0, n, &got, &d);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("%.2f ms per call (%zu bytes, %zu bytes of names)\n", ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / calls, n, got.n);
	}
	return 0;
}
