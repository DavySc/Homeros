/* frames a second of core_draw on a C file in a 60x200 window, no float:
   what lbr_cellw costs the ordinary draw path.  usage: bench_draw FILE N [wrap] */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "check.h"
#include "core.h"
#include "grid.h"
int main(int argc, char **argv)
{
	static Editor e;
	Grid g = { 0 };
	int n = argc > 2 ? atoi(argv[2]) : 2000;
	CHECK(argc > 1 && !core_open(&e, argv[1]) && !grid_resize(&g, 60, 200));
	if (argc > 3) core_set_wrap(&e, 1);
	core_goto(&e, 300, 0);
	for (int i = 0; i < 50; i++) core_draw(&e, NULL, &g);
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	for (int i = 0; i < n; i++) { core_changed = 1; core_draw(&e, NULL, &g); }
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("%.1f\n", ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / n / 1e3);
	return 0;
}
