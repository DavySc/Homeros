/* PR 102 review: the tests' fixture is grey, so red and blue swapped in a
   backend would pass them.  Shows IMG at (0, 0) under a compositor and takes
   a screenshot (grim) to OUT.  Usage: colour IMG OUT (colour.sh) */
#include <stdio.h>
#include <stdlib.h>
#include "backend.h"
#include "image.h"
#include "loop.h"

static void on_input(void *ctx, int fd) { int k[256]; (void)ctx; (void)fd; backend_wl.input(k, 256); }

int main(int argc, char **argv)
{
	int fds[2], rows, cols, cw, ch;
	char cmd[600];
	Grid g = { 0 }, shown = { 0 };
	if (argc < 3 || backend_wl.open()) return 1;
	for (int i = 0, k = backend_wl.fds(fds); i < k; i++) loop_watch(fds[i], on_input, NULL);
	for (int f = 0; f < 3; f++) {
		for (long long end = loop_now() + 300000000; loop_now() < end;) loop_once(10);
		backend_wl.size(&rows, &cols);
		backend_wl.cell(&cw, &ch);
		if (grid_resize(&g, rows, cols)) return 1;
		image_put(&g, 0, 0, image_load(argv[1], cols, rows, cw, ch, 0x1e1e2e), 0);
		while (!backend_wl.ready()) loop_once(-1);
		backend_wl.present(&g, &shown);
	}
	for (long long end = loop_now() + 300000000; loop_now() < end;) loop_once(10);
	snprintf(cmd, sizeof cmd, "grim %s", argv[2]);
	return system(cmd);
}
