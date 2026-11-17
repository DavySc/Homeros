/* Review of PR 12: cost of one uni_gprev at the end of a line with no ASCII
   byte, and of walking the whole line backwards (what holding `h` does).
   cc -O2 -Iinclude reviews/pr-12-repro/back.c src/uni/uni.c -o build/back; build/back BYTES */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "uni.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
	size_t n = argc > 1 ? strtoul(argv[1], 0, 10) : 3000;
	n -= n % 3;
	unsigned char *s = malloc(n);
	for (size_t i = 0; i < n; i += 3) memcpy(s + i, "\xe4\xb8\xad", 3);   /* 中 */
	double t = now();
	size_t b = uni_gprev(s, n, n);
	double one = now() - t;
	t = now();
	size_t steps = 0;
	for (size_t i = n; i > 0 && steps < 200; i = uni_gprev(s, n, i)) steps++;
	double all = now() - t;
	printf("%zu bytes: one step back from the end %.1f us (to %zu); %zu steps back %.1f ms\n", n, one * 1e6, b, steps, all * 1e3);
	return 0;
}
