/* Review of PR 110: what syntax_foldexpr costs after a one-byte edit, beside
   the reparse it follows, and what the first update of a language costs.
   usage: foldcost FILE [edits]   (FILE's ending picks the language) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "syntax.h"
#include "text.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	char *b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) return 1;
	Text *t = text_new(b, (size_t)n);
	int E = argc > 2 ? atoi(argv[2]) : 200, st;
	double t0 = now();
	Syntax *s = syntax_open(argv[1], t);
	if (!s) return puts("no syntax"), 1;
	syntax_update(s, t, NULL);
	double t1 = now();
	syntax_foldexpr(s, 0, &st);
	double t2 = now();
	double *up = malloc(sizeof *up * (size_t)E), *fo = malloc(sizeof *fo * (size_t)E);
	unsigned r = 1;
	for (int e = 0; e < E; e++) {
		r = r * 1103515245u + 12345u;
		size_t pos = text_line_start(t, (r >> 8) % text_lines(t));   /* as foldcost.lua */
		syntax_edit(s, t, pos, 0, " ", 1);
		text_insert(t, pos, " ", 1);
		/* the editor draws the window before it parses */
		static unsigned char fa[1024];
		size_t w = pos + sizeof fa < text_len(t) ? pos : 0;
		syntax_faces(s, w, w + (text_len(t) < sizeof fa ? text_len(t) : sizeof fa), fa);
		double a = now();
		syntax_update(s, t, NULL);
		double c = now();
		syntax_foldexpr(s, 0, &st);
		double d = now();
		up[e] = c - a, fo[e] = d - c;
		syntax_edit(s, t, pos, 1, "", 0);
		text_delete(t, pos, 1);
		syntax_update(s, t, NULL);
	}
	qsort(up, (size_t)E, sizeof *up, cmp);
	qsort(fo, (size_t)E, sizeof *fo, cmp);
	printf("%s: %ld bytes, %zu lines; open+first update %.2f ms, first foldexpr %.2f ms; per edit: reparse median %.3f p99 %.3f max %.3f ms, foldexpr median %.3f p99 %.3f max %.3f ms\n",
	       argv[1], n, text_lines(t), t1 - t0, t2 - t1, up[E / 2], up[E * 99 / 100], up[E - 1], fo[E / 2], fo[E * 99 / 100], fo[E - 1]);
	syntax_free(s);
	text_free(t);
	return 0;
}
