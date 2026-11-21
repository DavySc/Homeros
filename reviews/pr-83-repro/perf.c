/* Review of PR 83: what a frame's colours cost where the scan cannot start
   near the window.  Built as tests/org_drv.c is, with -O2 and no sanitizer
   (reviews/pr-83-repro/README.md).  Each row: median and range of 5 runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "org.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return (d > 0) - (d < 0); }
static unsigned char face[1 << 16];

/* type one byte at the end, update, then draw the last 4 KB: a keystroke */
static void keys(const char *name, const char *s, size_t n)
{
	double up[5], dr[5];
	for (int r = 0; r < 5; r++) {
		Text *t = text_new(s, n);
		OrgFx *x = orgfx_new(t);
		orgfx_update(x, NULL);
		double u = 0, d = 0;
		for (int i = 0; i < 100; i++) {
			size_t len = text_len(t);
			orgfx_edit(x, t, len, 0, 1);
			text_insert(t, len, "x", 1);
			double t0 = now();
			orgfx_update(x, NULL);
			double t1 = now();
			memset(face, 0, 4096);
			orgfx_faces(x, len + 1 - 4096, len + 1, face);
			u += t1 - t0; d += now() - t1;
		}
		up[r] = u / 100; dr[r] = d / 100;
		orgfx_free(x); text_free(t);
	}
	qsort(up, 5, sizeof *up, cmp); qsort(dr, 5, sizeof *dr, cmp);
	printf("%-34s %7zu bytes: update %.3f ms (%.3f to %.3f), 4 KB of faces after it %.3f ms (%.3f to %.3f)\n",
	       name, n, up[2], up[0], up[4], dr[2], dr[0], dr[4]);
}

/* draw the first screen of one long line */
static void line(const char *name, const char *unit, size_t n)
{
	char *s = malloc(n + 1);
	double dr[5];
	for (size_t i = 0; i < n; i++) s[i] = unit[i % strlen(unit)];
	for (int r = 0; r < 5; r++) {
		Text *t = text_new(s, n);
		OrgFx *x = orgfx_new(t);
		orgfx_update(x, NULL);
		double t0 = now();
		/* as src/core/draw.c asks: 256 bytes at a time along the line */
		for (size_t a = 0; a < 8192 && a < n; a += 256) {
			memset(face, 0, 256);
			orgfx_faces(x, a, a + 256, face);
		}
		dr[r] = now() - t0;
		orgfx_free(x); text_free(t);
	}
	qsort(dr, 5, sizeof *dr, cmp);
	printf("%-34s %7zu bytes: a screen (8 KB at most, 256-byte calls) %.3f ms (%.3f to %.3f)\n", name, n, dr[2], dr[0], dr[4]);
	free(s);
}

int main(void)
{
	static char s[1 << 20];
	size_t n = 0;
	for (int i = 0; n + 64 < 900000; i++) n += (size_t)sprintf(s + n, "some words on a line, number %d\n", i);
	keys("no headline, plain lines", s, n);
	for (n = 0; n + 64 < 900000;) {
		int i = (int)(n / 32);
		n += (size_t)sprintf(s + n, i % 50 ? "some words on a line, number %d\n" : "* headline %d\n", i);
	}
	keys("a headline every 50 lines", s, n);
	for (n = 0; n + 64 < 900000;) n += (size_t)sprintf(s + n, "- item %zu\n", n);
	keys("no headline, one long list", s, n);
	for (size_t k = 2048; k <= 131072; k *= 2) {
		if (k <= 32768) line("line of \"<2026-01-01 x \"", "<2026-01-01 x ", k);
		if (k <= 32768) line("line of \"<%%(x) \"", "<%%(x) ", k);
		line("line of plain words", "word ", k);
	}
	return 0;
}
