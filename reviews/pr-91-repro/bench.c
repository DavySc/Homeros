/* bench FILE LINE [KEYS]: KEYS (default 200) keys typed at LINE with 50 lines
   visible from LINE: per key, syntax_edit, the frame's syntax_faces for 50
   lines, then syntax_update with a stop callback that never stops but times
   the stretches between its calls.  Prints medians and maxima in ms. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }
static double last, longest;
static int stop(void) { double n = now(); if (n - last > longest) longest = n - last; last = n; return 0; }
static void frame(Syntax *s, Text *t, size_t l0)
{
	static unsigned char f[8192];
	for (size_t l = l0; l < l0 + 50 && l < text_lines(t); l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		for (; a < z; a += 1024) syntax_faces(s, a, a + 1024 < z ? a + 1024 : z, f);
	}
}
int main(int argc, char **argv)
{
	FILE *fp = fopen(argv[1], "rb");
	static char buf[64 << 20];
	size_t n = fread(buf, 1, sizeof buf, fp), l0 = (size_t)atol(argv[2]);
	int keys = argc > 3 ? atoi(argv[3]) : 200;
	Text *t = text_new(buf, n);
	Syntax *s = syntax_open(argv[1], t);
	double t0 = now();
	frame(s, t, l0);
	while (syntax_update(s, t, NULL)) ;
	frame(s, t, l0);
	double open = now() - t0;
	static double fr[10000], up[10000], st[10000], f2[10000];
	for (int k = 0; k < keys; k++) {
		size_t pos = text_line_start(t, l0 + 25);
		const char *ins = k % 7 == 3 ? "\n" : "x";
		syntax_edit(s, t, pos, 0, ins, 1);
		text_insert(t, pos, ins, 1);
		t0 = now();
		frame(s, t, l0);
		fr[k] = now() - t0;
		last = t0 = now(); longest = 0;
		while (syntax_update(s, t, stop)) ;
		stop();
		up[k] = now() - t0; st[k] = longest;
		t0 = now();
		frame(s, t, l0);
		f2[k] = now() - t0;
	}
	qsort(fr, keys, sizeof *fr, cmp); qsort(up, keys, sizeof *up, cmp); qsort(st, keys, sizeof *st, cmp);
	qsort(f2, keys, sizeof *f2, cmp);
	printf("frame after parse med %.3f max %.3f | ", f2[keys / 2], f2[keys - 1]);
	printf("open %.2f | frame med %.3f max %.3f | update med %.3f max %.3f | longest stretch med %.3f max %.3f\n",
	       open, fr[keys / 2], fr[keys - 1], up[keys / 2], up[keys - 1], st[keys / 2], st[keys - 1]);
	return 0;
}
