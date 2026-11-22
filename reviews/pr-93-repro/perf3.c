/* PR 93 review, head 200af04: a catch-up repeater now shifts until it is
   past today, one rewrite of the text per shift.  WAIT to DONE on
   "SCHEDULED: <DATE Thu 10:00 ++1h>" at the top of a text of n bytes.
   gcc -O2, median and range of 5 runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "org.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return (d > 0) - (d < 0); }
static char s[1 << 20];
int main(void)
{
	static const char *const date[] = { "2026-11-12 Thu", "2025-11-20 Thu", "2021-11-18 Thu", "1996-11-21 Thu" };
	for (size_t size = 1000; size <= 900000; size *= 30)
		for (int i = 0; i < 4; i++) {
			double d[5];
			size_t n = 0;
			for (int r = 0; r < 5; r++) {
				n = (size_t)sprintf(s, "* WAIT one\nSCHEDULED: <%s 10:00 ++1h>\n", date[i]);
				while (n + 64 < size) n += (size_t)sprintf(s + n, "some words on a line, number %zu\n", n);
				size_t a, b, len;
				double t0 = now();
				org_todo_step(s, n, 0, 1, 29918490, &a, &b, &len);
				d[r] = now() - t0;
			}
			qsort(d, 5, sizeof *d, cmp);
			printf("%7zu bytes, ++1h from %s: %.1f ms (%.1f to %.1f)\n", n, date[i], d[2], d[0], d[4]);
		}
	return 0;
}
