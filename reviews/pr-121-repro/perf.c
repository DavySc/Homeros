/* PR 121 review: per one-byte edit, the time of syntax_update (parse plus
   fold rows), median of 60 edits at random places.  perf FILE.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { return *(const double *)a < *(const double *)b ? -1 : 1; }
int main(int argc, char **argv)
{
	FILE *f = fopen(argv[argc - 1], "rb");
	static char b[1 << 20];
	size_t n = fread(b, 1, sizeof b, f), r1, r2;
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[argc - 1], t);
	static unsigned char face[4096];
	syntax_faces(s, 0, 4096, face);
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	double d[60];
	unsigned long long x = 88172645463325252ULL;
	for (int i = 0; i < 60; i++) {
		x ^= x << 13, x ^= x >> 7, x ^= x << 17;
		size_t pos = x % (n - 1);
		syntax_edit(s, t, pos, 0, i % 2 ? "\n" : "x", 1);
		text_insert(t, pos, i % 2 ? "\n" : "x", 1);
		syntax_faces(s, 0, 4096, face);
		double a = now();
		while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
		d[i] = now() - a;
	}
	qsort(d, 60, sizeof *d, cmp);
	printf("%.2f", d[30]);
	return 0;
}
