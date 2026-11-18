/* A slow reader of our selection: 1 MB is set as the clipboard and a client
   asks for it but reads nothing for 5 s.  How long does one input() call
   keep the editor's only thread?
   Usage: clip   (WAYLAND_DISPLAY = a headless sway; wtype, wl-paste on PATH) */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "backend.h"
#include "keys.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	int fds[2], k[64], r, c;
	if (backend_wl.open()) return 2;
	if (system("wtype -s 20000 & sleep 0.2")) return 2;
	int n = backend_wl.fds(fds);
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { n > 1 ? fds[1] : -1, POLLIN, 0 } };
	for (int i = 0; i < 150; i++) {
		usleep(10000);
		if (poll(p, 2, 10) > 0) backend_wl.input(k, 64);
		backend_wl.size(&r, &c);
		if (r != g.rows || c != g.cols) { grid_resize(&g, r, c); grid_free(&shown); }
		if (backend_wl.ready() && i % 10 == 0) { grid_puts(&g, 0, 0, "clip", 0); backend_wl.present(&g, &shown); }
	}
	if (system("wtype x")) return 2;             /* a key: the serial that owns the selection */
	for (double e = now() + 300; now() < e;) if (poll(p, 2, 10) > 0) backend_wl.input(k, 64);
	size_t len = 1 << 20;
	char *s = malloc(len);
	memset(s, 'x', len);
	printf("clip_set: %d\n", backend_wl.clip_set(s, len));
	if (system("(wl-paste -n | (sleep 5; wc -c > clip.count)) &")) return 2;
	double worst = 0;
	for (double e = now() + 8000; now() < e;) {
		if (poll(p, 2, 10) <= 0) continue;
		double a = now();
		backend_wl.input(k, 64);
		if (now() - a > worst) worst = now() - a;
	}
	printf("longest input() call: %.0f ms; reader got ", worst);
	fflush(stdout);
	if (system("cat clip.count")) return 2;
	backend_wl.close();
	return 0;
}
