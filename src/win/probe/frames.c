/* Build with the backend it measures, as src/win/check.sh links test_win
   (add -O2, drop the sanitizer); run on Windows or under Wine:
   frames.exe [runs].  Times backend_win's present on a window of code with
   the symbols Claude Code draws: the whole screen, one changed character
   (typing), and the text moved up a row (a terminal printing, a scroll),
   each the median of `runs` frames, in microseconds. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static const char *const line[] = {
	"static int pick(unsigned a, unsigned b, int after) { return a < b ? after : -1; }   // -> != ===",
	"╭── ✻ Claude Code ──╮ ▐▛███▜▌ ▝▜█████▛▘",
	"  ✦ Opus 5.5 │ 5h ━━── 29% ↻ 18:40 │ ⏵⏵ auto mode on",
	"theorem foo (f : α → ℝ) : ∀ᶠ x in 𝓝 a, ‖f x‖ ≤ ε",
};

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
	int runs = argc > 1 ? atoi(argv[1]) : 50, rows, cols;
	const Backend *b = &backend_win;
	Grid g = { 0 }, shown = { 0 };
	if (b->open()) return 1;
	SetWindowPos(FindWindowW(L"homeros", NULL), NULL, 0, 0, 1600, 900, SWP_NOMOVE | SWP_NOZORDER);
	static int k[1024];
	b->input(k, 1024);
	b->size(&rows, &cols);
	grid_resize(&g, rows, cols);
	fill(&g, 0);
	b->present(&g, &shown);
	double *t = malloc(sizeof *t * (size_t)runs);
	LARGE_INTEGER f, t0, t1;
	QueryPerformanceFrequency(&f);
	static const char *const what[] = { "whole screen", "one character", "moved up a row" };
	for (int w = 0; w < 3; w++) {
		for (int i = 0; i < runs; i++) {
			if (w == 0) grid_free(&shown);
			if (w == 1) grid_put(&g, rows / 2, 10, (const unsigned char *)(i & 1 ? "a" : "b"), 1, 1, ATTR_NONE);
			if (w == 2) fill(&g, i + 1);
			QueryPerformanceCounter(&t0);
			b->present(&g, &shown);
			QueryPerformanceCounter(&t1);
			t[i] = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)f.QuadPart;
		}
		qsort(t, (size_t)runs, sizeof *t, cmp);
		printf("%s (%d x %d): median %.0f us, range %.0f to %.0f\n", what[w], rows, cols, t[runs / 2], t[0], t[runs - 1]);
	}
	b->close();
	return 0;
}
