/* bench FILE WRAP N: N rounds of a fixed key string through keys_feed, no
   drawing (typeahead: core_update after every key); prints seconds */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "core.h"
#include "keys.h"
int main(int argc, char **argv)
{
	static Editor e; Grid g = {0};
	if (argc != 4 || core_open(&e, argv[1]) || grid_resize(&g, 50, 120)) return 2;
	core_set_wrap(&e, atoi(argv[2]));
	core_draw(&e, &g);
	const char *ks = "jjjjwwwjjkkkbbjjjj$0jjjkkkkkkkkkk";
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	for (int n = atoi(argv[3]); n--;) for (const char *s = ks; *s; s++) keys_feed(&e, *s, 0);
	clock_gettime(CLOCK_MONOTONIC, &b);
	core_draw(&e, &g);
	printf("%.3f\n", (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9);
	return 0;
}
