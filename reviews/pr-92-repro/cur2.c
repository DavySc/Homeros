/* PR 92: reviews/pr-82-repro/cur.c with an attr per cell, a start column
   (off) and more words.  PR 82, for any backend_wl in a headless sway:
   1. a word with a ligature, the cursor on no cell and then on each cell:
      per cell, the pixels that are not the cell's corner colour (its ink)
      and the commonest such colour.  Reversing a cell swaps two colours, so
      a cell's ink count must not depend on where the cursor is.
   2. the cursor moved one cell at a time (only the changed span is drawn):
      is the screen what a whole redraw gives?
   3. the same with a face per cell: whose colour does each part take?
   Usage: cur DIR */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "backend.h"
#include "font.h"
#include "keys.h"

static Grid g, shown;
static const char *dir;
static int cw, ch, bad, off;
static const unsigned char *attrs;
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
	pump(120);
}
static void shot(const char *name, const char *geom)
{
	char b[600];
	snprintf(b, sizeof b, "grim %s -t ppm %s/%s.ppm", geom, dir, name);
	FILE *f = popen(b, "r");
	struct pollfd p = { fileno(f), POLLIN, 0 };
	while (poll(&p, 1, 5) == 0) pump(5);
	if (pclose(f)) { printf("grim failed\n"); exit(2); }
}
enum { MAXC = 16 };
typedef struct { int ink[MAXC], top[MAXC], bg[MAXC]; } Row;
/* row 0, cells 0 to n: ink and commonest ink colour of each */
static void read_row(int n, Row *o)
{
	char geom[64], path[300];
	snprintf(geom, sizeof geom, "-g '%d,0 %dx%d'", off * cw, n * cw, ch);
	shot("r", geom);
	snprintf(path, sizeof path, "%s/r.ppm", dir);
	FILE *f = fopen(path, "rb");
	int pw, ph;
	if (!f || fscanf(f, "P6 %d %d 255%*c", &pw, &ph) != 2 || pw != n * cw || ph != ch) exit(2);
	static int px[64 * MAXC * 64];
	for (int i = 0; i < pw * ph; i++) {
		unsigned char p[3];
		if (fread(p, 1, 3, f) != 3) exit(2);
		px[i] = p[0] << 16 | p[1] << 8 | p[2];
	}
	fclose(f);
	for (int c = 0; c < n; c++) {
		int bgc = px[c * cw], best = -1, bestn = 0;
		o->bg[c] = bgc; o->ink[c] = 0;
		for (int y = 0; y < ch; y++) for (int x = c * cw; x < (c + 1) * cw; x++) {
			int v = px[y * pw + x], k = 0;
			if (v == bgc) continue;
			o->ink[c]++;
			for (int y2 = 0; y2 < ch; y2++) for (int x2 = c * cw; x2 < (c + 1) * cw; x2++) k += px[y2 * pw + x2] == v;
			if (k > bestn) { bestn = k; best = v; }
		}
		o->top[c] = best;
	}
}
static void row(const char *s, const unsigned char *face, int ccol)
{
	grid_clear(&g);
	int n = grid_puts(&g, 0, off, g.cols, s, strlen(s), ATTR_NONE, FACE_NONE);
	(void)n;
	if (face) for (int i = 0; face[i] != 255; i++) g.c[off + i].face = face[i];
	if (attrs) for (int i = 0; attrs[i] != 255; i++) g.c[off + i].attr = attrs[i];
	g.crow = ccol < 0 ? 5 : 0;
	g.ccol = ccol < 0 ? 0 : off + ccol;
}
static int same_as_whole(void)
{
	char cmd[900];
	shot("a", "");
	grid_free(&shown);
	present();
	shot("b", "");
	snprintf(cmd, sizeof cmd, "cmp -s %s/a.ppm %s/b.ppm", dir, dir);
	return system(cmd) == 0;
}
static void word(const char *s, int cells, const unsigned char *face)
{
	Row base, r;
	printf("\"%s\"%s%s at column %d\n", s, face ? " with a face per cell" : "", attrs ? " with an attr per cell" : "", off);
	for (int cur = -1; cur < cells; cur++) {
		row(s, face, cur);
		grid_free(&shown);
		present();
		read_row(cells, cur < 0 ? &base : &r);
		const Row *q = cur < 0 ? &base : &r;
		printf("  cursor %2d:", cur);
		for (int c = 0; c < cells; c++) {
			int d = q->ink[c] != base.ink[c];
			bad += d;
			printf(" [%3d %06x on %06x]%s", q->ink[c], q->top[c] < 0 ? 0 : q->top[c], q->bg[c], d ? "!" : "");
		}
		printf("\n");
	}
	/* the cursor walks the word, each step a partial redraw */
	int diff = 0;
	row(s, face, -1);
	grid_free(&shown);
	present(); present(); present();
	for (int cur = 0; cur < cells; cur++) {
		row(s, face, cur);
		present(); present(); present();   /* all buffers hold this one */
		row(s, face, cur + 1 < cells ? cur + 1 : -1);
		present();
		diff += !same_as_whole();
		present(); present();
	}
	bad += diff;
	printf("  cursor walked cell by cell: %d of %d steps differ from a whole redraw\n", diff, cells);
}
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	dir = argv[1];
	if (backend_wl.open()) { printf("open failed\n"); return 2; }
	int r, c, b;
	pump(200); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	pump(300); backend_wl.size(&r, &c); grid_resize(&g, r, c); present();
	font_cell(&cw, &ch, &b);
	printf("cell %dx%d; per cell: [ink, commonest ink colour, on background]\n", cw, ch);
	static const unsigned char F[] = { FACE_KEYWORD, FACE_STRING, FACE_NUMBER, FACE_NONE, 255 };
	static const unsigned char FV[] = { FACE_VISUAL, FACE_NONE, FACE_NONE, FACE_NONE, 255 };
	static const unsigned char FV2[] = { FACE_NONE, FACE_VISUAL, FACE_NONE, FACE_NONE, 255 };
	static const unsigned char R0[] = { ATTR_REV, ATTR_NONE, ATTR_NONE, ATTR_NONE, 255 };
	static const unsigned char R1[] = { ATTR_NONE, ATTR_REV, ATTR_NONE, ATTR_NONE, 255 };
	static const unsigned char R01[] = { ATTR_REV, ATTR_REV, ATTR_NONE, ATTR_NONE, 255 };
	static const unsigned char D0[] = { ATTR_DIM, ATTR_NONE, ATTR_NONE, ATTR_NONE, 255 };
#define SPILL "\xe2\x9f\xb9"
	for (int pass = 0; pass < 2; pass++) {
		off = pass ? 7 : 0;
		word(SPILL "  b", 4, NULL);
		word(SPILL "  b", 4, F);
		word(SPILL "  b", 4, FV);
		word(SPILL "  b", 4, FV2);
		word("a" SPILL " b", 4, NULL);
		word("a" SPILL " b", 4, F);
		word("\xe4\xb8\xad\xe4\xb8\xad", 4, NULL);            /* two wide characters */
		word("\xe4\xb8\xad->", 4, NULL);                      /* a ligature after a wide one */
		word("\xf0\x9f\x91\x8d  b", 4, NULL);                 /* a colour glyph */
		word("->" SPILL " ", 4, NULL);
		word("a\xe2\x86\x94" SPILL " ", 4, NULL);
		attrs = R0; word(SPILL "  b", 4, NULL);
		attrs = R1; word(SPILL "  b", 4, NULL);
		attrs = R01; word(SPILL "  b", 4, NULL);
		attrs = D0; word(SPILL "  b", 4, NULL);
		attrs = R0; word("a-->", 4, NULL);
		attrs = R1; word("a-->", 4, NULL);
		attrs = NULL;
	}
	/* the symbol in the row's last cell, and before it with the blank last */
	off = c - 2; word("a" SPILL, 2, NULL);
	off = c - 3; word("a" SPILL " ", 3, NULL);
	off = c - 2; word("\xe4\xb8\xad", 2, NULL);
	printf("%d checks failed\n", bad);
	backend_wl.close();
	return bad != 0;
}
