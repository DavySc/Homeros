/* reviews/package-8-repro/pix.c with another pool, for PR 68: the characters
   of JetBrains Mono's ligatures and six symbols whose glyph starts left of
   its cell (bear.c).  Incremental drawing against a whole redraw, for any backend_wl: random
   rows of tokens, edits, cursor moves, scrolls both
   ways with a relative number column, sideways scrolls.  Every CHECK_EVERY
   frames the screen (grim) must equal the screen after a whole redraw.
   Usage: pix SEED FRAMES DIR   (WAYLAND_DISPLAY = a headless sway) */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "backend.h"
#include "keys.h"

enum { MAXR = 200, MAXT = 400 };
static int CHECK_EVERY = 20;
typedef struct { const char *s; int w; unsigned char attr, face; } Tok;
static Tok rows[MAXR][MAXT];
static int ntok[MAXR], xoff, top;
static Grid g, shown;
static unsigned long long rs;
static unsigned rnd(unsigned n) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33) % n; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static const struct { const char *s; int w; } POOL[] = {
	{"a",1},{"x",1},{" ",1},{" ",1},{" ",1},{" ",1},{"-",1},{">",1},{"=",1},{"<",1},{"/",1},{"\\",1},{"|",1},{"!",1},{":",1},
	{"\xe2\x86\x94",1},{"\xe2\x87\x94",1},{"\xe2\x9f\xb7",1},{"\xe2\x84\x89",1},{"\xe2\x9f\xb9",1},{"\xce\x8c",1},
};
static void gen_row(int r)
{
	ntok[r] = (int)rnd(MAXT);
	if (rnd(5) == 0) ntok[r] = 0;
	unsigned char face = 0, attr = 0;
	for (int i = 0; i < ntok[r]; i++) {
		if (rnd(6) == 0) { face = (unsigned char)rnd(FACE_COUNT); attr = rnd(8) ? 0 : (unsigned char)rnd(3); }
		unsigned k = rnd(sizeof POOL / sizeof *POOL);
		rows[r][i] = (Tok){ POOL[k].s, POOL[k].w, attr, face };
	}
}
static void render(void)
{
	grid_clear(&g);
	for (int r = 0; r < g.rows; r++) {
		int m = (top + r) % MAXR, c = 0;
		char num[8];
		int rel = r - g.crow;
		snprintf(num, sizeof num, "%3d ", rel < 0 ? -rel : rel ? rel : top + r);
		c = grid_puts(&g, r, 0, num, ATTR_DIM);
		for (int i = xoff; i < ntok[m] && c + rows[m][i].w <= g.cols; i++) {
			int w = grid_put(&g, r, c, (const unsigned char *)rows[m][i].s, strlen(rows[m][i].s), rows[m][i].w, rows[m][i].attr);
			if (!w) break;
			g.c[r * g.cols + c].face = rows[m][i].face;
			c += w;
		}
	}
}
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
}
static void fit(void)
{
	int r, c;
	backend_wl.size(&r, &c);
	if (r != g.rows || c != g.cols) grid_resize(&g, r, c);
}
static int shot(const char *dir, const char *name)
{
	char b[600];
	pid_t pid;
	snprintf(b, sizeof b, "grim -t ppm %s/%s.ppm", dir, name);
	FILE *f = popen(b, "r");   /* keep answering the compositor meanwhile */
	(void)pid;
	int fd = fileno(f);
	struct pollfd p = { fd, POLLIN, 0 };
	while (poll(&p, 1, 5) == 0) pump(5);
	return pclose(f);
}
int main(int argc, char **argv)
{
	if (argc < 4) return 2;
	rs = strtoull(argv[1], NULL, 10) * 2654435761u + 7;
	int frames = atoi(argv[2]), bad = 0;
	if (getenv("EVERY")) CHECK_EVERY = atoi(getenv("EVERY"));
	const char *dir = argv[3];
	if (backend_wl.open()) { printf("open failed\n"); return 2; }
	for (int r = 0; r < MAXR; r++) gen_row(r);
	pump(200); fit(); render(); present(); pump(300); fit(); render(); present();
	for (int f = 1; f <= frames; f++) {
		pump(1); fit();
		int op = (int)rnd(10), top0 = top, cr0 = g.crow;
		switch (op) {
		case 0: case 1: top = (top + 1 + (int)rnd(4)) % MAXR; break;                 /* scroll down */
		case 2: case 3: top = (top + MAXR - 1 - (int)rnd(4)) % MAXR; break;          /* scroll up */
		case 4: xoff = (int)rnd(2) ? xoff + 1 : xoff > 0 ? xoff - 1 : 0; if (xoff > 60) xoff = 0; break;
		case 5: gen_row((top + (int)rnd((unsigned)g.rows)) % MAXR); break;
		case 6: top = (int)rnd(MAXR); break;                                         /* jump */
		default: {                                                                    /* edit some tokens */
			for (int i = (int)rnd(5); i >= 0; i--) {
				int m = (top + (int)rnd((unsigned)g.rows)) % MAXR;
				if (!ntok[m]) { gen_row(m); continue; }
				unsigned k = rnd(sizeof POOL / sizeof *POOL);
				Tok *t = &rows[m][rnd((unsigned)ntok[m] < 100 ? (unsigned)ntok[m] : 100)];
				t->s = POOL[k].s; t->w = POOL[k].w;
				if (rnd(3) == 0) t->face = (unsigned char)rnd(FACE_COUNT);
			}
		} }
		g.crow = (int)rnd((unsigned)g.rows);
		render();
		g.ccol = (int)rnd((unsigned)g.cols);
		present();
		if (f % CHECK_EVERY == 0) {
			char cmd[900];
			pump(120);
			if (shot(dir, "a")) { printf("grim failed\n"); return 2; }
			grid_free(&shown);
			present();
			pump(120);
			shot(dir, "b");
			snprintf(cmd, sizeof cmd, "cmp -s %s/a.ppm %s/b.ppm", dir, dir);
			if (system(cmd)) {
				bad++;
				snprintf(cmd, sizeof cmd, "cp %s/a.ppm %s/bad-%s-%d-a.ppm; cp %s/b.ppm %s/bad-%s-%d-b.ppm", dir, dir, argv[1], f, dir, dir, argv[1], f);
				if (system(cmd)) {}
				printf("seed %s frame %d: screen differs from a whole redraw (op %d, top %d -> %d, cursor row %d -> %d col %d, xoff %d)\n", argv[1], f, op, top0, top, cr0, g.crow, g.ccol, xoff);
			}
		}
	}
	printf("seed %s: %d frames, %d of %d checks differ\n", argv[1], frames, bad, frames / CHECK_EVERY);
	backend_wl.close();
	return bad != 0;
}
