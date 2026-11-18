/* Review of PR 51: allocations per key, after first use.  Opens a file,
   parses it, draws 50 lines from line argv[2]; then 200 times: types one
   character inside the window, draws the frame (syntax_faces per line), and
   lets syntax_update finish.  malloc, calloc and realloc are counted, split
   into the frame and the update; the scanners' own calls (the grammars'
   scanner.c are compiled with malloc renamed, alloc.sh) are counted apart.  Also timed: each
   key's update, and its longest stretch between two calls of stop. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
void *__libc_malloc(size_t), *__libc_calloc(size_t, size_t), *__libc_realloc(void *, size_t);
static long calls, scanner;
void *malloc(size_t n) { calls++; return __libc_malloc(n); }
void *calloc(size_t a, size_t b) { calls++; return __libc_calloc(a, b); }
void *realloc(void *p, size_t n) { calls++; return __libc_realloc(p, n); }
void *rv_scanner_malloc(size_t n) { scanner++; return __libc_malloc(n); }
void *rv_scanner_calloc(size_t a, size_t b) { scanner++; return __libc_calloc(a, b); }
void *rv_scanner_realloc(void *p, size_t n) { scanner++; return __libc_realloc(p, n); }

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
/* the longest stretch an update runs without asking stop */
static double last, gap;
static int ask(void) { double t = now(); if (t - last > gap) gap = t - last; last = t; return 0; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static void frame(Syntax *s, const Text *t, size_t l0)
{
	static unsigned char face[8192];
	for (size_t l = l0; l < l0 + 50 && l < text_lines(t); l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		syntax_faces(s, a, z - a > 8192 ? a + 8192 : z, face);
	}
}
int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	static char b[1 << 22];
	size_t n = fread(b, 1, sizeof b, f), l0 = (size_t)atol(argv[2]);
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[1], t);
	if (!t || !s) return 2;
	frame(s, t, l0);
	while (syntax_update(s, t, NULL)) ;
	frame(s, t, l0);
	/* warm-up: 20 keys, so every buffer has grown */
	long fa = 0, up = 0, sc = 0;
	static double ms[200], gaps[200];
	for (int k = 0; k < 220; k++) {
		size_t pos = text_line_start(t, l0 + 25) + (size_t)(k % 3);
		if (k == 20) fa = up = sc = 0;
		syntax_edit(s, t, pos, 0, "x", 1);
		if (text_insert(t, pos, "x", 1)) return 2;
		long c0 = calls, s0 = scanner;
		frame(s, t, l0);
		fa += calls - c0 + scanner - s0;
		c0 = calls, s0 = scanner;
		double t0 = last = now();
		while (syntax_update(s, t, ask)) ;
		ask();
		if (k >= 20) ms[k - 20] = now() - t0, gaps[k - 20] = gap;
		gap = 0;
		up += calls - c0, sc += scanner - s0;
		frame(s, t, l0);
	}
	printf("%s line %zu, 200 keys: %ld allocations in frames, %ld in updates outside the scanners (%.1f per key), %ld in the scanners (%.1f per key)\n",
	       argv[1], l0, fa, up, up / 200.0, sc, sc / 200.0);
	qsort(ms, 200, sizeof *ms, cmp), qsort(gaps, 200, sizeof *gaps, cmp);
	printf("  update per key: median %.2f ms, p99 %.2f, max %.2f; longest stretch without asking stop: median %.2f ms, p99 %.2f, max %.2f\n",
	       ms[100], ms[197], ms[199], gaps[100], gaps[197], gaps[199]);
	return 0;
}
