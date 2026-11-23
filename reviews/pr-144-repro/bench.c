/* review bench: index time and the time to paint a 50-line window, line by line as the core asks */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	int lang = argc > 2 ? atoi(argv[2]) : 3;
	long top = argc > 3 ? atol(argv[3]) : -1;   /* window's first line; -1: the last 50 */
	fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
	char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) return 1;
	Text *t = text_new(b, n);
	static unsigned char face[1 << 22];
	double best_i = 1e18, best_w = 1e18, first_w = 0;
	for (int rep = 0; rep < 5; rep++) {
		Tsql *q = tsql_new(t, lang);
		double t0 = now();
		while (tsql_update(q, NULL) == 2) ;
		double t1 = now();
		size_t L = text_lines(t), l0 = top >= 0 ? (size_t)top : (L > 50 ? L - 50 : 0);
		for (int fr = 0; fr < 3; fr++) {
			double w0 = now();
			for (size_t l = l0; l < l0 + 50 && l <= L; l++) {
				size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
				if (z - a > sizeof face) z = a + sizeof face;
				memset(face, 0, z - a);
				tsql_faces(q, a, z, face);
			}
			double w1 = now();
			if (!rep && !fr) first_w = w1 - w0;
			if (w1 - w0 < best_w) best_w = w1 - w0;
		}
		if (t1 - t0 < best_i) best_i = t1 - t0;
		tsql_free(q);
	}
	printf("%s: %ld bytes, index %.2f ms (%.0f MB/s), 50-line window %.3f ms (first %.3f)\n", argv[1], n, best_i, n / 1e6 / (best_i / 1e3), best_w, first_w);
	return 0;
}
