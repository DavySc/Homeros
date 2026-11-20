/* What the walk back to a word's start costs: present() alone, for whole
   redraws of rows with no blank at all (the longest walk) and of rows of
   8-letter words.  Prints the median of N presents in microseconds.
   Usage: cost N   (in a headless sway) */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "backend.h"
#include "keys.h"
static Grid g, shown;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void pump(double ms)
{
	int fds[2], k[64], n = backend_wl.fds(fds);
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { n > 1 ? fds[1] : -1, POLLIN, 0 } };
	for (double end = now() + ms; now() < end;) {
		if (poll(p, 2, (int)(end - now()) + 1) <= 0) continue;
		backend_wl.input(k, 64);
	}
}
static double present(void)
{
	for (double end = now() + 3000; !backend_wl.ready() && now() < end;) pump(1);
	if (!backend_wl.ready()) { printf("never ready\n"); exit(2); }
	double t = now();
	backend_wl.present(&g, &shown);
	return now() - t;
}
static int cmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return d < 0 ? -1 : d > 0; }
int main(int argc, char **argv)
{
	int N = argc > 1 ? atoi(argv[1]) : 100, r, c;
	static double t[10000];
	if (backend_wl.open()) return 2;
	pump(200); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	pump(300); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	for (int kind = 0; kind < 2; kind++) {
		for (int i = 0; i < g.rows; i++) for (int j = 0; j < g.cols; j++)
			grid_put(&g, i, j, (const unsigned char *)(kind && j % 9 == 8 ? " " : "a"), 1, 1, ATTR_NONE);
		for (int i = 0; i < N + 5; i++) { grid_free(&shown); double d = present(); if (i >= 5) t[i - 5] = d; }
		qsort(t, (size_t)N, sizeof *t, cmp);
		printf("%s %dx%d: median %.0f us (%.0f to %.0f) ", kind ? "words" : "no blank", g.rows, g.cols, t[N / 2] * 1e3, t[0] * 1e3, t[N - 1] * 1e3);
	}
	printf("\n");
	backend_wl.close();
	return 0;
}
