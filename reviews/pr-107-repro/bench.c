/* Speed of the fold module on a 200,000-line outline: a heading every 10
   lines, levels cycling 1..4.  usage: bench (prints ns) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fold.h"
enum { N = 200000 };
static unsigned char lv[N], st[N];
static int fe(void *c, size_t l, int *s) { (void)c; *s = st[l]; return lv[l]; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
int main(void)
{
	int cur = 0;
	for (size_t l = 0; l < N; l++) {
		if (l % 10 == 0) { cur = 1 + (int)(l / 10 % 4); st[l] = 1; }
		lv[l] = (unsigned char)cur;
	}
	Fold *f = fold_new(1, fe, NULL, NULL);
	double t0 = now();
	fold_update(f, 0, N, N);
	double t1 = now();
	enum { R = 100000 };
	for (int i = 0; i < R; i++) { size_t l = (size_t)(i * 7919LL % N); fold_update(f, l, l + 1, N); }
	double t2 = now();
	size_t a, b; unsigned long sum = 0;
	for (int i = 0; i < R; i++) { size_t top = (size_t)(i * 7919LL % (N - 60)); for (size_t l = top; l < top + 60; l++) sum += (unsigned long)fold_closed(f, l, &a, &b) + (unsigned long)fold_level(f, l); }
	double t3 = now();
	/* a change at the top that moves every line: adjust + update */
	for (int i = 0; i < 1000; i++) { fold_adjust(f, 5, (size_t)-1, 1, 0, 0); fold_update(f, 5, 6, N); fold_adjust(f, 5, 6, FOLD_DEL, -1, 0); fold_update(f, 5, 5, N); }
	double t4 = now();
	printf("full %.2f ms  one-line %.0f ns  screen60 %.0f ns  insert+delete at top %.0f ns  (%lu)\n", (t1 - t0) / 1e6, (t2 - t1) / R, (t3 - t2) / R, (t4 - t3) / 1000, sum);
	fold_free(f);
	return 0;
}
