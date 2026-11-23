/* PR 102 review: the frame an image appears whole (a jump, not a scroll, so
   the backend's row copy does not help), against the frame the same cells
   appear as text.  Even frames show text A; odd frames show, in the box the
   image takes, the image (mode 1) or text B (mode 0).  Times present() of
   the odd frames, and image_load's hit (the core asks every frame).
   Usage: jump IMG N 0|1 (under a compositor: jump.sh) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "backend.h"
#include "image.h"
#include "keys.h"
#include "loop.h"

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
	long long look = 0;
	for (int f = -1; f < 2 * n + 10; f++) {
		if (f < 0 || resized) {
			for (long long end = loop_now() + 300000000; loop_now() < end;) loop_once(10);
			resized = 0;
			f = 0;
			backend_wl.size(&rows, &cols);
			backend_wl.cell(&cw, &ch);
			if (grid_resize(&g, rows, cols)) return 1;
			if (!image_load(argv[1], cols, rows / 2, cw, ch, 0x1e1e2e)) return 1;
		}
		long long a = loop_now();
		id = image_load(argv[1], cols, rows / 2, cw, ch, 0x1e1e2e);
		look += loop_now() - a;
		image_size(id, &ic, &ir);
		for (int r = 0; r < rows; r++) {
			int in = r >= 5 && r < 5 + ir, k = snprintf(s, sizeof s, "%4d  In a lattice every pair has a join and a meet; x <= y iff x v y = y.  %d", r, r * 7919 % 1000);
			if (in && f % 2) k = snprintf(s, sizeof s, "%4d  A chain is a poset in which any two elements compare; an antichain none.", r);
			grid_puts(&g, r, 0, cols, s, (size_t)k, ATTR_NONE, FACE_NONE);
			for (int c = k; c < cols; c++) grid_puts(&g, r, c, cols, " ", 1, ATTR_NONE, FACE_NONE);
			if (in && f % 2 && !img) for (int c = ic; c < cols; c++) grid_puts(&g, r, c, cols, " ", 1, ATTR_NONE, FACE_NONE);
		}
		if (img && f % 2) image_put(&g, 5, 0, id, 0);
		while (!backend_wl.ready()) loop_once(-1);
		a = loop_now();
		backend_wl.present(&g, &shown);
		if (f >= 10 && f % 2) t[(f - 10) / 2] = (loop_now() - a) / 1e6;
	}
	qsort(t, (size_t)n, sizeof *t, cmp);
	printf("%s: %dx%d cells, box %dx%d cells, present median %.3f ms p99 %.3f ms, image_load hit %.0f ns\n",
	       img ? "image" : "text", cols, rows, ic, ir, t[n / 2], t[n * 99 / 100], (double)look / (2 * n + 10));
	backend_wl.close();
	return 0;
}
