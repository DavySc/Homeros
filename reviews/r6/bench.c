/* backend_present on a 60x200 grid to /dev/null: one line changed, every cell changed */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include "backend.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	int saved = dup(1); dup2(open("/dev/null", O_WRONLY), 1);
	grid_resize(&g, 60, 200);
	backend_present(&g, &shown);
	enum { N = 2000 };
	double t = now();
	for (int f = 0; f < N; f++) {
		for (int c = 0; c < 200; c++) grid_put(&g, 30, c, (const unsigned char *)(f & 1 ? "a" : "b"), 1, 1, 0);
		backend_present(&g, &shown);
	}
	double line = (now() - t) / N;
	t = now();
	for (int f = 0; f < N; f++) {
		for (int r = 0; r < 60; r++) for (int c = 0; c < 200; c++) grid_put(&g, r, c, (const unsigned char *)(f & 1 ? "a" : "b"), 1, 1, 0);
		backend_present(&g, &shown);
	}
	double all = (now() - t) / N;
	dprintf(saved, "%.1f %.1f\n", line, all);
	return 0;
}
