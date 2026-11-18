/* Review of PR 26: time per key, keys_feed alone (no drawing).
   usage: perf FILE N MODE   MODE i: N characters typed in Insert mode at the
   end of line 1; w: N times "w" then "b" pairs... (w: N w motions, wrapping
   with gg); x: N times "x" at the start of line 1 followed by "u". */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "core.h"
int main(int argc, char **argv)
{
	Editor e;
	if (argc < 4 || core_open(&e, argv[1])) return 1;
	long n = atol(argv[2]);
	struct timespec a, b;
	if (argv[3][0] == 'i') keys_feed(&e, 'A', 0);
	clock_gettime(CLOCK_MONOTONIC, &a);
	for (long i = 0; i < n; i++) {
		switch (argv[3][0]) {
		case 'i': keys_feed(&e, 'a' + (int)(i % 26), 0); break;
		case 'w': keys_feed(&e, i % 64 == 63 ? '0' : 'w', 0); break;
		case 'x': keys_feed(&e, i % 2 ? 'u' : 'x', 0); break;
		case 'j': keys_feed(&e, i % 2 ? 'k' : 'j', 0); break;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("%.3f", ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (double)n / 1000);
	return 0;
}
