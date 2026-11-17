/* Review of package 12: how long syntax_update runs without asking stop.
   syntax.h says stop is asked "now and then (package 12 measured at most
   0.86 ms apart)".  Types KEYS characters at random places of FILE (a
   40-line window around each is shown first, as the editor's frame does);
   after each, syntax_update runs with a stop that never stops but records
   the longest time between two of its calls, counting the start of the
   update and its return as calls.
   usage: gap FILE KEYS SEED */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static double last, gap;
static int calls;
static int stop(void)
{
	double n = now();
	if (n - last > gap) gap = n - last;
	last = n;
	calls++;
	return 0;
}
static unsigned long long rng;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static unsigned char fc[1 << 16];

static void show(Syntax *s, const Text *t, size_t l0)
{
	size_t lines = text_lines(t);
	for (size_t l = l0; l < l0 + 40 && l < lines; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		if (z - a < sizeof fc) syntax_faces(s, a, z, fc);
	}
}

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	static char b[1 << 21];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	int nk = atoi(argv[2]);
	rng = 2 * strtoull(argv[3], NULL, 10) + 1;
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[1], t);
	if (!s) return 3;
	show(s, t, 0);
	last = now(), gap = 0;
	while (syntax_update(s, t, stop)) ;
	stop();
	printf("%s: first parse: longest %.3f ms without asking stop (%d asks)\n", argv[1], gap, calls);
	double *g = malloc((size_t)nk * sizeof *g);
	static const char *const ty = "x = foo(a, b); /* c */\n";
	size_t l0 = 0, pos = 0;
	for (int i = 0; i < nk; i++) {
		if (i % 23 == 0) {            /* a new place, its window shown and parsed */
			l0 = rnd((unsigned)text_lines(t));
			show(s, t, l0);
			while (syntax_update(s, t, NULL)) ;
			pos = text_line_start(t, l0 + (l0 + 20 < text_lines(t) ? 20 : 0));
		}
		char c = ty[i % 23];
		syntax_edit(s, t, pos, 0, &c, 1);
		if (text_insert(t, pos, &c, 1)) return 4;
		pos++;
		show(s, t, l0);               /* the frame after the key */
		last = now(), gap = 0;
		while (syntax_update(s, t, stop)) ;
		stop();
		g[i] = gap;
	}
	qsort(g, (size_t)nk, sizeof *g, cmp);
	int over = 0;
	for (int i = 0; i < nk; i++) over += g[i] > 0.86;
	printf("%s: %d keys: longest time without asking stop: median %.3f ms, p99 %.3f, max %.3f; over 0.86 ms after %d keys\n",
	       argv[1], nk, g[nk / 2], g[nk * 99 / 100], g[nk - 1], over);
	syntax_free(s);
	text_free(t);
	return 0;
}
