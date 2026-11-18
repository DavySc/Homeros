/* Review of PR 56: core_draw's time on a Markdown-heavy screen, and its
   allocations after the first frames.  Opens FILE in a ROWSxCOLS grid,
   parses it, then draws FRAMES frames, a `j` (or `k` at the end) between
   them so the cursor line, and with it the raw heading, moves.  Counts
   malloc/calloc/realloc calls made inside core_draw (ld --wrap) after 50
   warm-up frames.  build.sh builds it for a tree.
   usage: bench FILE FRAMES [wrap] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "grid.h"
#include "syntax.h"

static long allocs; static int counting;
void *__real_malloc(size_t n); void *__real_calloc(size_t a, size_t b); void *__real_realloc(void *p, size_t n);
void *__wrap_malloc(size_t n) { allocs += counting; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { allocs += counting; return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { allocs += counting; return __real_realloc(p, n); }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	int nf = atoi(argv[2]);
	static Editor e;
	Grid g = { 0 };
	if (core_open(&e, argv[1]) || grid_resize(&g, 50, 200)) return 1;
	if (argc > 3) core_set_wrap(&e, 1);
	do core_draw(&e, &g); while (syntax_update(e.buf->syn, e.buf->text, NULL));
	double *t = __real_malloc((size_t)nf * sizeof *t);
	size_t nl = core_lines(&e);
	int dir = 'j';
	unsigned long sum = 0;
	for (int i = -50; i < nf; i++) {
		if (e.win->line + 1 >= nl) dir = 'k'; else if (!e.win->line) dir = 'j';
		keys_feed(&e, dir, 0);
		core_update(&e);
		while (syntax_update(e.buf->syn, e.buf->text, NULL)) ;
		counting = i >= 0;
		double t0 = now();
		core_draw(&e, &g);
		double t1 = now();
		counting = 0;
		if (i >= 0) t[i] = t1 - t0;
		for (int c = 0; c < g.rows * g.cols; c += 97) sum += g.c[c].ch[0];
	}
	qsort(t, (size_t)nf, sizeof *t, cmp);
	printf("%d frames: median %.1f us, p99 %.1f, max %.1f; %ld allocations in core_draw (sum %lu)\n", nf, t[nf / 2], t[nf * 99 / 100], t[nf - 1], allocs, sum);
	return 0;
}
