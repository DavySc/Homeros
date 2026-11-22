/* Frame time with and without 'hlsearch' (review of PR 106).
   usage: bench FILE PATTERN|- [rows cols]: prints ns per core_draw, the
   median of N frames (BENCH_N, default 2000), with the cursor on line 100 of the file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "keys.h"
#include "../src/core/core_int.h"

static int cmp(const void *a, const void *b) { long long x = *(const long long *)a, y = *(const long long *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	static Editor e;
	static long long t[2000];
	int n = getenv("BENCH_N") ? atoi(getenv("BENCH_N")) : 2000;
	Grid g = { 0 };
	int rows = argc > 3 ? atoi(argv[3]) : 50, cols = argc > 4 ? atoi(argv[4]) : 200;
	if (argc < 3 || core_open(&e, argv[1]) || grid_resize(&g, rows, cols)) return 1;
	layout_resize(&e, rows, cols);
	if (strcmp(argv[2], "-")) {
		keys_feed(&e, '/', 1);
		for (const char *p = argv[2]; *p; p++) keys_feed(&e, *p, 1);
		keys_feed(&e, KEY_ENTER, 0);
	}
	e.win->line = 100, e.win->col = 0;
	core_update(&e);
	for (int i = 0; i < (n < 20 ? 1 : 20); i++) core_draw(&e, &g);
	for (int i = 0; i < n; i++) {
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		core_draw(&e, &g);
		clock_gettime(CLOCK_MONOTONIC, &b);
		t[i] = (b.tv_sec - a.tv_sec) * 1000000000LL + b.tv_nsec - a.tv_nsec;
	}
	qsort(t, (size_t)n, sizeof *t, cmp);
	printf("%lld\n", t[n / 2]);
	return 0;
}
