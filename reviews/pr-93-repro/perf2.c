/* PR 93 review: what orgfx_foldexpr over every line and one org_todo_step
   cost on a 900 KB text (a headline every 50 lines, or none), -O2, no
   sanitizer.  Median and range of 5 runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "org.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return (d > 0) - (d < 0); }
static char s[1 << 20];
static void run(const char *name, size_t n)
{
	double fo[5], td[5], te[5];
	size_t lines = 0;
	for (int r = 0; r < 5; r++) {
		Text *t = text_new(s, n);
		OrgFx *x = orgfx_new(t);
		orgfx_update(x, NULL);
		lines = text_lines(t);
		double t0 = now();
		int st, sum = 0;
		for (size_t l = 0; l < lines; l++) sum += orgfx_foldexpr(x, l, &st);
		fo[r] = now() - t0;
		size_t a, b, len;
		t0 = now();
		org_todo_step(s, n, n / 2, 1, 29918490, &a, &b, &len);
		td[r] = now() - t0;
		t0 = now();
		org_todo_step(s, n, n - 1, 1, 29918490, &a, &b, &len);
		te[r] = now() - t0;
		orgfx_free(x); text_free(t);
		if (sum < 0) abort();
	}
	qsort(fo, 5, sizeof *fo, cmp); qsort(td, 5, sizeof *td, cmp); qsort(te, 5, sizeof *te, cmp);
	printf("%-28s %zu bytes, %zu lines: foldexpr on every line %.3f ms (%.3f to %.3f); one TODO step in the middle %.3f ms (%.3f to %.3f), at the end %.3f ms (%.3f to %.3f)\n",
	       name, n, lines, fo[2], fo[0], fo[4], td[2], td[0], td[4], te[2], te[0], te[4]);
}
int main(void)
{
	size_t n = 0;
	for (n = 0; n + 64 < 900000;) { int i = (int)(n / 32); n += (size_t)sprintf(s + n, i % 50 ? "some words on a line, number %d\n" : "* TODO headline %d\n", i); }
	run("a headline every 50 lines", n);
	n = (size_t)sprintf(s, "* TODO one\n");
	for (; n + 64 < 900000;) n += (size_t)sprintf(s + n, "some words on a line, number %zu\n", n);
	run("one headline, long body", n);
	/* one entry of k repeating stamps, WAIT to DONE: each stamp parses the entry again */
	for (size_t k = 100; k <= 3200; k *= 2) {
		double d[5];
		for (int r = 0; r < 5; r++) {
			n = (size_t)sprintf(s, "* WAIT one\n");
			for (size_t i = 0; i < k; i++) n += (size_t)sprintf(s + n, "<2026-11-01 Sun +1d> x\n");
			size_t a, b, len;
			double t0 = now();
			org_todo_step(s, n, 0, 1, 29918490, &a, &b, &len);
			d[r] = now() - t0;
		}
		qsort(d, 5, sizeof *d, cmp);
		printf("one entry of %zu repeating stamps (%zu bytes), WAIT to DONE: %.1f ms (%.1f to %.1f)\n", k, n, d[2], d[0], d[4]);
	}
	return 0;
}
