/* Times the org parser: a full parse of the files given (best of 5 in one
   run), then 2,000 one-byte edits at random places in the first file, each
   followed by org_edit; prints the median and the 99th percentile.
   gcc -O2 -Iinclude -Isrc/org src/org/oracle/bench.c src/org/org.c src/text/text.c ref/uni.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "org.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	Text *t[64];
	int n = argc - 1;
	size_t bytes = 0, nodes = 0;
	for (int i = 0; i < n; i++) {
		FILE *f = fopen(argv[i + 1], "rb");
		fseek(f, 0, SEEK_END); long k = ftell(f); rewind(f);
		char *b = malloc((size_t)k);
		if (fread(b, 1, (size_t)k, f) != (size_t)k) return 1;
		fclose(f);
		t[i] = text_new(b, (size_t)k); bytes += (size_t)k; free(b);
	}
	double best = 1e9;
	for (int r = 0; r < 5; r++) {
		double t0 = now();
		nodes = 0;
		for (int i = 0; i < n; i++) { Org *o = org_new(t[i]); nodes += org_count(o); org_free(o); }
		double d = now() - t0;
		if (d < best) best = d;
	}
	printf("full parse: %d files, %zu bytes, %zu elements: %.2f ms\n", n, bytes, nodes, best * 1e3);
	Org *o = org_new(t[0]);
	static double e[2000];
	srand(1);
	for (int i = 0; i < 2000; i++) {
		size_t len = text_len(t[0]), pos = (size_t)rand() % (len + 1);
		const char *s = i % 2 ? "x" : "\n";
		double t0 = now();
		text_insert(t[0], pos, s, 1);
		org_edit(o, t[0], pos, 0, 1);
		e[i] = now() - t0;
	}
	qsort(e, 2000, sizeof *e, cmp);
	printf("edit (insert + org_edit) in %s, %zu elements: median %.1f us, p99 %.1f us, max %.1f us\n",
	       argv[1], org_count(o), e[1000] * 1e6, e[1980] * 1e6, e[1999] * 1e6);
	return 0;
}
