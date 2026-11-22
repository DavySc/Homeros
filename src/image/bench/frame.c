/* Frame time with and without an image (package 21): the Wayland backend's
   present() over a full grid of text scrolled a row per frame, with or
   without an image of IMG on it (as a Hasse diagram in notes).  Prints the
   median and p99 of present() in ms over N frames.
   Usage: frame IMG N 0|1 (under a compositor: src/image/bench/run.sh) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "backend.h"
#include "image.h"
#include "keys.h"
#include "loop.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int resized;
static void on_input(void *ctx, int fd)
{
	int k[256];
	(void)ctx; (void)fd;
	for (int i = 0, n = backend_wl.input(k, 256); i < n; i++) resized |= k[i] == KEY_RESIZE;
}
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	int n = argc > 2 ? atoi(argv[2]) : 0, img = argc > 3 && atoi(argv[3]), fds[2], rows, cols, cw, ch;
	static double t[100000];
	Grid g = { 0 }, shown = { 0 };
	if (argc < 4 || n < 1 || n > 100000 || backend_wl.open()) return 1;
	for (int i = 0, k = backend_wl.fds(fds); i < k; i++) loop_watch(fds[i], on_input, NULL);
	char s[512];
	int id = 0, ic = 0, ir = 0;
	for (int f = -1; f < n + 10; f++) {
		if (f < 0 || resized) {   /* sway tiles the window once it is mapped: start again at its size */
			for (double end = now() + 300; now() < end;) loop_once(10);
			resized = 0;
			f = 0;
			backend_wl.size(&rows, &cols);
			backend_wl.cell(&cw, &ch);
			if (grid_resize(&g, rows, cols)) return 1;
			id = image_load(argv[1], cols, rows / 2, cw, ch, 0x1e1e2e);
			image_size(id, &ic, &ir);
			if (!id) return 1;
		}
		/* a document of 3 rows per image row: lines of text, the image from line 5 */
		for (int r = 0; r < rows; r++) {
			int l = (f + r) % (3 * rows);
			int k = snprintf(s, sizeof s, "%4d  In a lattice every pair has a join and a meet; x <= y iff x v y = y.  %d", l, l * 7919 % 1000);
			grid_puts(&g, r, 0, cols, s, (size_t)k, ATTR_NONE, FACE_NONE);
			if (img && l >= 5 && l < 5 + ir) image_put(&g, r, 0, id, l - 5);
		}
		while (!backend_wl.ready()) loop_once(-1);
		double a = now();
		backend_wl.present(&g, &shown);
		if (f >= 10) t[f - 10] = now() - a;   /* the first frames draw everything */
	}
	qsort(t, (size_t)n, sizeof *t, cmp);
	printf("%s: %dx%d cells, image %dx%d cells, present median %.3f ms p99 %.3f ms\n",
	       img ? "image" : "text", cols, rows, ic, ir, t[n / 2], t[n * 99 / 100]);
	backend_wl.close();
	return 0;
}
