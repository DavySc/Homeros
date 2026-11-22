/* PR 78 probe: time per Insert-mode key in a 700 KB .sql buffer.
   mode 0: keys_feed only (main without snip); 1: through snip_key (the PR);
   2: through snip_key inside an expanded snippet with mirrors */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "snip.h"
static Editor e;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static void feed(int mode, int k) { if (mode == 0 || e.ks.pending || !snip_key(&e, k, 0)) keys_feed(&e, k, 0); }
int main(int argc, char **argv)
{
	int mode = atoi(argv[2]);
	if (core_open(&e, argv[1])) return 2;
	e.win->line = core_lines(&e) / 2; e.win->col = 0;
	feed(mode, 'o');
	if (mode >= 2) { for (const char *s = "lay-hide-group"; *s; s++) feed(mode, *s); feed(mode, 25); if (mode == 3) feed(mode, 9); }
	const char *w = "select abc from dbo.t where x = 1 and lay";
	double t[2000]; int n = 0;
	for (int rep = 0; rep < 40; rep++)
		for (const char *s = w; *s; s++) { double a = now(); feed(mode, *s); t[n++] = now() - a; }
	double tot = 0, mx = 0; for (int i = 0; i < n; i++) { tot += t[i]; if (t[i] > mx) mx = t[i]; }
	int c(const void *x, const void *y) { double d = *(double*)x - *(double*)y; return (d > 0) - (d < 0); }
	qsort(t, n, sizeof *t, c);
	size_t L; const unsigned char *l = core_line(&e, e.win->line, &L); if (mode == 3) fprintf(stderr, "%.*s\n", (int)(L > 300 ? 300 : L), l);
	printf("mode %d keys %d mean %.2f us median %.2f p99 %.2f max %.2f\n", mode, n, tot / n, t[n/2], t[n*99/100], mx);
	return 0;
}
