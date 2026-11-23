/* The performance gates of DESIGN.md's Goals for the window, measured on the
   editor's own loop (src/main.c's, with clocks added): first frame committed
   after start, and frame ready after a key (or after the frame callback, if
   the key came while a frame was on its way), p50/p99/max, and present()'s
   own share (the backend's; the rest is the core's drawing).  Keys come from
   outside (wtype under sway, real typing on Hyprland).  Stops after QUIET ms
   without keys once N keys have come.  Prints one line.
   Usage: bench FILE N */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "backend.h"
#include "core.h"
#include "loop.h"

enum { BATCH = 64, QMAX = 4096, MAXF = 100000, QUIET = 1000 };
static int q[QMAX], qh, qn, resized, nkeys;
static double tkey, frames[MAXF], pres[MAXF];   /* pres: present() alone */   /* tkey: the oldest key not yet drawn */
static int nf;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void on_input(void *ctx, int fd)
{
	(void)ctx; (void)fd;
	int k[256];
	if (qn + 256 > QMAX) return;
	int n = backend_wl.input(k, 256);
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_RESIZE) { resized = 1; continue; }
		if (!tkey) tkey = now();
		q[(qh + qn++) % QMAX] = k[i];
		nkeys++;
	}
}

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	double t0 = now(), first = 0, last = 0, tready = 0;
	static Editor e;
	Grid g = { 0 }, shown = { 0 };
	int fds[2], rows, cols, want = argc > 2 ? atoi(argv[2]) : 0;
	if (argc < 2 || core_open(&e, argv[1]) || backend_wl.open()) return 1;
	for (int i = 0, n = backend_wl.fds(fds); i < n; i++) loop_watch(fds[i], on_input, NULL);
	backend_wl.size(&rows, &cols);
	if (grid_resize(&g, rows, cols)) return 1;
	int dirty = 1;
	while (!e.quit && !(nkeys >= want && last && now() - last > QUIET)) {
		if (resized) {
			resized = 0;
			backend_wl.size(&rows, &cols);
			if (grid_resize(&g, rows, cols)) break;
			dirty = 1;
		}
		for (int b = 0; b < BATCH && qn && !e.quit; b++, dirty = 1) {
			int k = q[qh];
			qh = (qh + 1) % QMAX;
			qn--;
			keys_feed(&e, k, qn > 0);
		}
		if (dirty && backend_wl.ready()) {
			double a = tkey > tready ? tkey : tready;
			core_draw(&e, NULL, &g);
			double p = now();
			backend_wl.present(&g, &shown);
			dirty = 0;
			double z = now();
			if (!first) first = z - t0;
			else if (tkey && nf < MAXF) { pres[nf] = z - p; frames[nf++] = z - a; }
			if (tkey) last = z;
			tkey = 0;
		}
		int ready = backend_wl.ready();
		if (loop_once(qn ? 0 : nkeys >= want && last ? QUIET : -1)) break;
		if (!ready && backend_wl.ready()) tready = now();
	}
	qsort(frames, (size_t)nf, sizeof *frames, cmp);
	qsort(pres, (size_t)nf, sizeof *pres, cmp);
	printf("first frame %.2f ms; %d keys, %d frames; frame ready p50 %.3f p99 %.3f max %.3f ms; present p50 %.3f p99 %.3f ms\n",
	       first, nkeys, nf, nf ? frames[nf / 2] : 0, nf ? frames[nf * 99 / 100] : 0, nf ? frames[nf - 1] : 0,
	       nf ? pres[nf / 2] : 0, nf ? pres[nf * 99 / 100] : 0);
	backend_wl.close();
	grid_free(&g);
	grid_free(&shown);
	core_free(&e);
	return 0;
}
