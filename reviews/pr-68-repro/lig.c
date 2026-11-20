/* PR 68, two questions, for any backend_wl in a headless sway:
   1. a row drawn as OLD, then changed to NEW (only the changed span is
      drawn): is the screen what a whole redraw of NEW gives?
   2. "a->b" with the cursor on each cell: how many pixels of the "-" cell
      are neither the background nor the cursor's block?
   3. a symbol whose glyph starts left of its cell (bear.c), after a blank
      and after a letter: the ink of the cell before it, whole redraws
   Usage: lig DIR */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "backend.h"
#include "keys.h"

static Grid g, shown;
static const char *dir;
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
static void present(void)
{
	for (double end = now() + 3000; !backend_wl.ready() && now() < end;) pump(2);
	if (!backend_wl.ready()) { printf("never ready\n"); exit(2); }
	backend_wl.present(&g, &shown);
	pump(150);
}
static void shot(const char *name, const char *geom)
{
	char b[600];
	snprintf(b, sizeof b, "grim %s -t ppm %s/%s.ppm", geom, dir, name);
	FILE *f = popen(b, "r");   /* keep answering the compositor meanwhile */
	struct pollfd p = { fileno(f), POLLIN, 0 };
	while (poll(&p, 1, 5) == 0) pump(5);
	if (pclose(f)) { printf("grim failed\n"); exit(2); }
}
/* pixels of cell c of row 0 that are not its corner's colour */
static int cell_ink(int c, int cw, int ch, int *first)
{
	char geom[64], path[300];
	snprintf(geom, sizeof geom, "-g '%d,0 %dx%d'", c * cw, cw, ch);
	shot("c", geom);
	snprintf(path, sizeof path, "%s/c.ppm", dir);
	FILE *f = fopen(path, "rb");
	int pw, ph, n = 0;
	*first = -1;
	if (!f || fscanf(f, "P6 %d %d 255%*c", &pw, &ph) != 2) exit(2);
	for (int i = 0; i < pw * ph; i++) {
		unsigned char p[3];
		if (fread(p, 1, 3, f) != 3) exit(2);
		int v = p[0] << 16 | p[1] << 8 | p[2];
		if (*first < 0) *first = v;
		n += v != *first;
	}
	fclose(f);
	return n;
}
static void row(const char *s, int ccol)
{
	grid_clear(&g);
	grid_puts(&g, 0, 0, g.cols, s, strlen(s), ATTR_NONE, FACE_NONE);
	g.crow = ccol < 0 ? 5 : 0;
	g.ccol = ccol < 0 ? 0 : ccol;
}
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	dir = argv[1];
	if (backend_wl.open()) { printf("open failed\n"); return 2; }
	int r, c, bad = 0;
	pump(200); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	pump(300); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	static const char *const T[][2] = {
		{ " a", " \xe2\x86\x94" }, { " \xe2\x86\x94", " a" },           /* ↔ at a word start */
		{ " \xe2\x87\x94 b", " a b" }, { " a b", " \xe2\x84\x89 b" },   /* ⇔, ℉ */
		{ "x \xe2\x86\x94", "x a" }, { "xa", "x\xe2\x86\x94" },
		{ "a ->", "a =>" }, { "a -> b", "a -- b" }, { "x /\\ y", "x /a y" }, { "x /a y", "x /\\ y" },
		{ " -> ", " ->>" }, { " != ", " !a " }, { "a<=b", "a<-b" },
	};
	char cmd[900];
	for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
		row(T[i][0], -1);
		grid_free(&shown);
		present();
		present();      /* twice more: both buffers hold OLD, whichever is free next */
		present();
		row(T[i][1], -1);
		present();
		shot("a", "");
		grid_free(&shown);
		present();
		shot("b", "");
		snprintf(cmd, sizeof cmd, "cmp -s %s/a.ppm %s/b.ppm", dir, dir);
		int d = system(cmd) != 0;
		bad += d;
		printf("\"%s\" then \"%s\": %s\n", T[i][0], T[i][1], d ? "DIFFERS from a whole redraw" : "same");
		if (d) { snprintf(cmd, sizeof cmd, "cp %s/a.ppm %s/bad-%zu-a.ppm; cp %s/b.ppm %s/bad-%zu-b.ppm", dir, dir, i, dir, dir, i); if (system(cmd)) {} }
	}
	/* "x /\ y" typed a character at a time, the cursor after it */
	grid_clear(&g); g.crow = g.ccol = 0;
	grid_free(&shown);
	present(); present(); present();
	for (int i = 1; i <= 6; i++) {
		char t[8] = "x /\\ y";
		t[i] = 0;
		row(t, i);
		present();
	}
	shot("a", "");
	grid_free(&shown);
	present();
	shot("b", "");
	snprintf(cmd, sizeof cmd, "cmp -s %s/a.ppm %s/b.ppm", dir, dir);
	{ int d = system(cmd) != 0; bad += d; printf("\"x /\\ y\" typed: %s\n", d ? "DIFFERS from a whole redraw" : "same"); }
	/* the cursor on each cell of "a->b": the ink of the "-" cell (cell 1) */
	int cw = 0, ch = 0;
	{ extern void font_cell(int *, int *, int *); int b; font_cell(&cw, &ch, &b); }
	for (int cur = -1; cur < 4; cur++) {
		row("a->b", cur);
		grid_free(&shown);
		present();
		int first, n = cell_ink(1, cw, ch, &first);
		printf("cursor on cell %d: the \"-\" cell has %d pixels that are not its background (%06x)\n", cur, n, first);
	}
	static const char *const W[] = { "  ", " \xe2\x86\x94", " \xe2\x84\x89", "a ", "a\xe2\x86\x94", "a\xe2\x84\x89", "a\xe2\x8c\xab" };
	for (size_t i = 0; i < sizeof W / sizeof *W; i++) {
		int first;
		row(W[i], -1);
		grid_free(&shown);
		present();
		printf("\"%s\": cell 0 has %d pixels of ink\n", W[i], cell_ink(0, cw, ch, &first));
	}
	backend_wl.close();
	return bad != 0;
}
