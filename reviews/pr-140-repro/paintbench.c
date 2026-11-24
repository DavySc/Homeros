/* Review of PR 140: what paint() costs for one changed character (typing)
   and a text moved up a row, on Linux, the same grid as
   src/win/probe/frames.c; with -DPARTS, the typing frame's parts.
   Build (from the repository's root, after make):
     cc -O2 -Iinclude -Itests -I/usr/include/stb reviews/pr-140-repro/paintbench.c src/paint/paint.c src/font/font.c \
       src/font/fc.c src/font/sprite.c src/grid/grid.c ref/theme.c src/uni/uni.c src/image/image.c build/image/stb.o \
       src/file/file.c src/file/eol.c src/file/journal.c src/file/state.c src/loop/loop.c src/loop/timer.c src/text/text.c \
       $(pkg-config --cflags --libs freetype2 harfbuzz fontconfig) -lm -o build/paintbench
   and the same with paint.c patched by span0.diff. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_font_env.h"
#include "font.h"
#include "grid.h"
#include "paint.h"

static const char *const line[] = {
	"static int pick(unsigned a, unsigned b, int after) { return a < b ? after : -1; }   // -> != ===",
	"╭── ✻ Claude Code ──╮ ▐▛███▜▌ ▝▜█████▛▘",
	"  ✦ Opus 5.5 │ 5h ━━── 29% ↻ 18:40 │ ⏵⏵ auto mode on",
	"theorem foo (f : α → ℝ) : ∀ᶠ x in 𝓝 a, ‖f x‖ ≤ ε",
};
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void fill(Grid *g, int from)
{
	for (int r = 0; r < g->rows; r++) {
		const char *s = line[(r + from) % 4];
		grid_puts(g, r, 0, g->cols, s, strlen(s), ATTR_NONE, (r + from) % 3 ? FACE_NONE : FACE_STRING);
	}
}

int main(int argc, char **argv)
{
	int runs = argc > 1 ? atoi(argv[1]) : 200, rows = 39, cols = 128, cw, ch, cb;
	font_env();
	if (font_open("JetBrains Mono", 16)) return 1;
	font_cell(&cw, &ch, &cb);
	Grid g = { 0 }, shown = { 0 };
	grid_resize(&g, rows, cols);
	grid_resize(&shown, rows, cols);
	uint32_t *px = malloc(sizeof *px * (size_t)(cols * cw) * (size_t)(rows * ch));
	unsigned char dirty[rows];
	double *t = malloc(sizeof *t * (size_t)runs);
	fill(&g, 0);
	paint(px, cols * cw, &g, &shown, 1, dirty);
	static const char *const what[] = { "whole screen", "one character", "moved up a row" };
	for (int w = 0; w < 3; w++) {
		for (int i = 0; i < runs; i++) {
			if (w == 1) grid_put(&g, rows / 2, 10, (const unsigned char *)(i & 1 ? "a" : "b"), 1, 1, ATTR_NONE);
			if (w == 2) fill(&g, i + 1);
			double t0 = now();
			paint(px, cols * cw, &g, &shown, w == 0, dirty);
			t[i] = now() - t0;
		}
		qsort(t, (size_t)runs, sizeof *t, cmp);
		printf("%s: median %.1f us, range %.1f to %.1f\n", what[w], t[runs / 2], t[0], t[runs - 1]);
	}
	return 0;
}
