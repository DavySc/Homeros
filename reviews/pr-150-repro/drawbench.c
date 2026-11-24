/* PR 150 review: the cost of a typing frame, main against the PR.  Types
   KEYS characters into FILE (insert mode, one per frame) in a 60 x 200
   grid, keys_feed then core_draw per key, and prints the median and p99
   of the frame (keys + draw) in microseconds.  DRAW(e, g) is core_draw's
   call on either side.  usage: drawbench FILE KEYS */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "core.h"
#include "keys.h"
#include "grid.h"
#ifndef DRAW
#define DRAW(e, g) core_draw(e, NULL, g)
#endif
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
int main(int argc, char **argv)
{
	static Editor e;
	static Grid g;
	int n = argc > 2 ? atoi(argv[2]) : 2000;
	double *t = malloc(n * sizeof *t);
	if (argc < 2 || !t || core_open(&e, argv[1]) || grid_resize(&g, 60, 200)) return 1;
	core_goto(&e, 500, 0);
	DRAW(&e, &g);
	keys_feed(&e, 'o', 0);
	DRAW(&e, &g);
	for (int i = 0; i < n; i++) {
		double a = now();
		keys_feed(&e, i % 60 == 59 ? KEY_ENTER : 'a' + i % 26, 0);
		DRAW(&e, &g);
		t[i] = now() - a;
	}
	qsort(t, n, sizeof *t, cmp);
	printf("%.1f %.1f\n", t[n / 2], t[n * 99 / 100]);
	keys_feed(&e, KEY_ESC, 0);
	core_free(&e);
	return 0;
}
