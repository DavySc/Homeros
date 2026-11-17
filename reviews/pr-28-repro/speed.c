/* PR 28 review: ns per text_line_start over every line of about 2.2 MB with
   a '\n' every `every` bytes (argv[1]).  CPU time, 3 passes, best pass. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "text.h"
int main(int argc, char **argv)
{
	size_t every = argc > 1 ? (size_t)atol(argv[1]) : 1, n = 2200000;
	char *s = malloc(n);
	for (size_t i = 0; i < n; i++) s[i] = i % every == every - 1 ? '\n' : 'a';
	Text *t = text_new(s, n);
	size_t lines = text_lines(t), sink = 0;
	int reps = lines < 100000 ? 20 : 1;   /* enough lookups to time */
	double best = 1e9;
	for (int pass = 0; pass < 3; pass++) {
		struct timespec a, b;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a);
		for (int r = 0; r < reps; r++)
			for (size_t l = 0; l <= lines; l++) sink += text_line_start(t, l);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &b);
		double ns = ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / ((double)reps * ((double)lines + 1));
		if (ns < best) best = ns;
	}
	printf("%.1f\n", best + (double)(sink & 0));
	return 0;
}
