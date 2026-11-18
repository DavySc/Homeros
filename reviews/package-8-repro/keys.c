/* Keys left in the backend's queue: 8 keys arrive in one read, input() is
   asked for 4; do the backend's fds then say there is more?
   Usage: keys   (WAYLAND_DISPLAY = a headless sway, wtype on PATH) */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "backend.h"
#include "keys.h"
int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	int fds[2], k[64], r, c, got = 0;
	if (backend_wl.open()) return 2;
	/* a keyboard that stays, so the seat keeps its keyboard while wtype types */
	if (system("wtype -s 20000 & sleep 0.2")) return 2;
	int n = backend_wl.fds(fds);
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { n > 1 ? fds[1] : -1, POLLIN, 0 } };
	for (int i = 0; i < 150; i++) {
		usleep(10000);             /* map, take the size, draw */
		if (poll(p, 2, 10) > 0) backend_wl.input(k, 64);
		backend_wl.size(&r, &c);
		if (r != g.rows || c != g.cols) { grid_resize(&g, r, c); grid_free(&shown); }
		if (backend_wl.ready() && i % 10 == 0) { grid_puts(&g, 0, 0, "keys", 0); backend_wl.present(&g, &shown); }
	}
	printf("size %dx%d ready %d\n", r, c, backend_wl.ready());
	if (system("wtype -d 1 abcdefgh")) return 2;
	usleep(300000);                              /* all 8 keys now wait in the socket */
	for (int round = 0; round < 6; round++) {
		int m = poll(p, 2, 500) > 0 ? backend_wl.input(k, 4) : -1;
		printf("round %d: m=%d %s", round, m, m < 0 ? "fds quiet" : "");
		for (int i = 0; i < m; i++) if (k[i] != KEY_RESIZE) { printf("%c", k[i] == 27 ? '^' : k[i]); got++; }
		printf("\n");
	}
	printf("%d of 8 keys delivered\n", got);
	backend_wl.close();
	return got != 8;
}
