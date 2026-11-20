/* drawbench FILE MODE N: N core_draw calls of a 50x120 window; MODE 0 no
   Visual, 118 v, 86 V, 22 block from line 0 col 100 to line 45 col 250;
   prints microseconds per frame */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "core.h"
#include "keys.h"
int main(int argc, char **argv)
{
	static Editor e; Grid g = {0};
	if (argc != 4 || core_open(&e, argv[1]) || grid_resize(&g, 50, 120)) return 2;
	core_set_wrap(&e, 0);
	int mode = atoi(argv[2]), n = atoi(argv[3]);
	e.win->line = 45; e.win->col = 250; core_set_want(&e); core_update(&e);
#ifdef HAVE_VISUAL
	e.ks.visual = mode; e.ks.vline = 0; e.ks.vcol = 100;
#endif
	core_draw(&e, &g);
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	for (int i = 0; i < n; i++) core_draw(&e, &g);
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("%.1f\n", ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / n);
	return mode < 0;
}
