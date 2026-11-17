/* The thin editor: one buffer, a terminal, the event loop of Render.tla
   ("ok": draw only between commands, at most BATCH commands per frame). */
#include <stdio.h>
#include <string.h>
#include "backend.h"
#include "core.h"
#include "loop.h"

enum { BATCH = 64, QMAX = 4096 };

static int q[QMAX], qh, qn;     /* keys read but not yet run */
static int resized;

static void on_input(void *ctx, int fd)
{
	(void)ctx; (void)fd;
	int k[256];
	if (qn + 256 > QMAX) return;      /* run what is queued first */
	int n = backend_input(k, 256);
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_RESIZE) { resized = 1; continue; }
		q[(qh + qn++) % QMAX] = k[i];
	}
}

int main(int argc, char **argv)
{
	static Editor e;
	Grid g = { 0 }, shown = { 0 };
	int fds[2], rows, cols;
	if (argc != 2) { fprintf(stderr, "usage: homeros FILE\n"); return 2; }
	if (core_open(&e, argv[1])) { fprintf(stderr, "homeros: %s\n", e.msg); return 1; }
	if (backend_open()) { fprintf(stderr, "homeros: needs a terminal\n"); return 1; }
	for (int i = 0, n = backend_fds(fds); i < n; i++) loop_watch(fds[i], on_input, NULL);
	backend_size(&rows, &cols);
	if (grid_resize(&g, rows, cols)) goto out;
	int dirty = 1;
	while (!e.quit) {
		if (resized) {
			resized = 0;
			backend_size(&rows, &cols);
			if (grid_resize(&g, rows, cols)) break;
			dirty = 1;
		}
		for (int b = 0; b < BATCH && qn && !e.quit; b++, dirty = 1) {
			int k = q[qh];
			qh = (qh + 1) % QMAX;
			qn--;
			if (k == KEY_CTRL_Z && e.mode == MODE_NORMAL) {
				backend_suspend();
				grid_free(&shown);     /* the terminal was used meanwhile: redraw all */
			} else keys_feed(&e, k, qn > 0);
		}
		if (e.quit) break;
		if (dirty && backend_ready()) {
			core_draw(&e, &g);
			backend_present(&g, &shown);
			dirty = 0;
		}
		if (loop_once(qn ? 0 : -1)) break;
	}
out:
	backend_close();
	grid_free(&g);
	grid_free(&shown);
	core_free(&e);
	return 0;
}
