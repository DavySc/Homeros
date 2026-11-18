/* Review of PR 20: cost of core_draw, core_update and core_edit on a large
   buffer, and of an edit when a mark is set and many undo groups exist.
   usage: perf LINES GROUPS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void rep(const char *what, double *v, int n) { qsort(v, n, sizeof *v, cmp); printf("%-44s median %.4f ms  p99 %.4f  max %.4f (n=%d)\n", what, v[n / 2], v[n * 99 / 100], v[n - 1], n); }
int main(int argc, char **argv)
{
	long lines = atol(argv[1]), groups = atol(argv[2]);
	char path[] = "/tmp/perfXXXXXX";
	int fd = mkstemp(path); (void)fd;
	FILE *f = fopen(path, "w");
	for (long i = 0; i < lines; i++) fprintf(f, "\tline %ld with some text (foo, bar) and more words to fill eighty columns or so\n", i);
	fclose(f);
	static Editor e; Grid g = { 0 };
	if (core_open(&e, path) || grid_resize(&g, 50, 160)) return 1;
	static double v[200000];
	int n = 0;
	for (int w = 0; w < 2; w++) {
		core_set_wrap(&e, w);
		n = 0;
		for (int i = 0; i < 2000; i++) {
			e.win->line = (size_t)(rand() % lines); e.win->col = 0; core_set_want(&e);
			double t = now(); core_update(&e); core_draw(&e, &g); v[n++] = now() - t;
		}
		rep(w ? "jump + draw, wrap" : "jump + draw, nowrap", v, n);
	}
	core_set_wrap(&e, 0);
	n = 0;
	for (int i = 0; i < 2000; i++) {
		e.win->line = (size_t)(lines / 2); e.win->col = 0;
		size_t at = core_line_start(&e, e.win->line);
		double t = now(); core_edit(&e, at, 0, "x", 1); undo_close(e.buf->undo); core_update(&e); core_draw(&e, &g); v[n++] = now() - t;
	}
	rep("edit + draw, no mark set", v, n);
	e.buf->mark[0] = (Mark){ 0, 0, 1 };
	for (int side = 0; side < 2; side++) {
		n = 0;
		for (long i = 0; i < groups; i++) {
			size_t at = core_line_start(&e, (size_t)(lines / 2));
			double t = now(); core_edit(&e, at, 0, "x", 1); undo_close(e.buf->undo); v[n++] = now() - t;
		}
		printf("after %ld groups with mark a set: last 100 edits mean %.4f ms\n", groups * (side + 1), ({ double s = 0; for (int i = n - 100; i < n; i++) s += v[i]; s / 100; }));
	}
	n = 0;
	for (int i = 0; i < 200; i++) { double t = now(); core_undo(&e, 0); v[n++] = now() - t; }
	rep("undo with the groups above", v, n);
	core_free(&e); unlink(path);
	return 0;
}
