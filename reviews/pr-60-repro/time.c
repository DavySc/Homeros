/* times sqlfmt on a file: argv[2] runs, prints each run's ms, then peak RSS */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>
#include "text.h"
#include "sqlfmt.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	static char buf[1 << 26];
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(buf, 1, sizeof buf, f);
	Text *t = text_new(buf, n);
	size_t ra = argc > 3 ? strtoul(argv[3], 0, 10) : 0, rz = argc > 4 ? strtoul(argv[4], 0, 10) : n;
	for (int i = 0; i < atoi(argv[2]); i++) {
		size_t a = ra, z = rz, m; const char *r;
		double t0 = now();
		int k = sqlfmt(t, &a, &z, &r, &m);
		printf("%.2f ms kept=%d edit=[%zu,%zu)->%zu\n", now() - t0, k, a, z, m);
	}
	struct rusage u; getrusage(RUSAGE_SELF, &u);
	printf("peak RSS %ld KB for %zu bytes\n", u.ru_maxrss, n);
	return 0;
}
