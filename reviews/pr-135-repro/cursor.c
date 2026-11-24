/* The cursor on a cell whose face draws base-coloured text on a colour
   (comment.todo and its three neighbours in ref/theme.c): reversed, its
   background is the base, which wl.c then makes clear, so the wallpaper
   shows in the cursor's block; src/win/backend.c keeps a reversed cell
   opaque.  Shows a red wallpaper's window for SECONDS, the cursor on a blank
   of that face at the top left, for a screenshot (cursor.sh).
   Usage: cursor SECONDS (under a compositor) */
#define _GNU_SOURCE
#include <stdlib.h>
#include <time.h>
#include "backend.h"
#include "keys.h"
#include "loop.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void on_input(void *ctx, int fd) { int k[256]; (void)ctx; (void)fd; backend_wl.input(k, 256); }

int main(int argc, char **argv)
{
	int fds[2], rows, cols, face = FACE_NONE;
	Grid g = { 0 }, shown = { 0 };
	if (argc < 2 || backend_wl.open()) return 1;
	for (int i = 0, k = backend_wl.fds(fds); i < k; i++) loop_watch(fds[i], on_input, NULL);
	/* the first face that draws the base colour on a background of its own */
	for (int i = 0; i < FACES && face == FACE_NONE; i++)
		if ((faces[i].style & (FACE_FG | FACE_BG)) == (FACE_FG | FACE_BG) && faces[i].fg[0] == 30 && faces[i].fg[1] == 30 && faces[i].fg[2] == 46) face = i;
	if (face == FACE_NONE) return 2;
	for (double end = now() + atof(argv[1]) * 1e3; now() < end;) {
		loop_once(50);
		if (!backend_wl.ready()) continue;
		backend_wl.size(&rows, &cols);
		if (grid_resize(&g, rows, cols)) return 1;
		grid_puts(&g, 0, 0, cols, "    ", 4, ATTR_NONE, face);
		g.crow = g.ccol = 0;
		backend_wl.present(&g, &shown);
	}
	backend_wl.close();
	return 0;
}
