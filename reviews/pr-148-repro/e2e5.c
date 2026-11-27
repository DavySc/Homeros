/* The real editor core on a .lean file with the real Lean server: waits
   until lsp_faces differs from the grammar's faces (the tokens came), then
   prints per byte the grammar's face and the face drawn.  e2e FILE SECONDS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int never(void) { return 0; }
int main(int argc, char **argv)
{
	Editor e;
	if (core_open(&e, argv[1])) { fprintf(stderr, "core_open failed\n"); return 1; }
	fprintf(stderr, "lsp %p syn %p faces==lsp_faces %d\n", (void *)e.buf->lsp, (void *)e.buf->syn, e.buf->faces == lsp_faces);
	size_t n = text_len(e.buf->text), r1, r2;
	unsigned char *g = malloc(n + 1), *f = malloc(n + 1);
	char *b = malloc(n + 1);
	text_read(e.buf->text, 0, b, n);
	double t0 = now(), first = 0, worst = 0;
	int edited = argc < 4;
	while (now() - t0 < atof(argv[2]) * 1000) {
		if (!edited && now() - t0 > 12000) {   /* a space typed at the end of line argv[3] */
			edited = 1;
			core_edit(&e, text_line_start(e.buf->text, (size_t)atoi(argv[3])) - 1, 0, " ", 1);
			lsp_flush(e.buf->lsp);
			n = text_len(e.buf->text); g = realloc(g, n + 1); f = realloc(f, n + 1);
		}
		double a = now();
		loop_once(5);
		if (now() - a - 5 > worst) worst = now() - a - 5;
		if (e.buf->syn) while (syntax_update(e.buf->syn, e.buf->text, never, &r1, &r2) == 2) ;
		static double last; if (now() - last < 500) continue; last = now();   /* the whole file's faces twice a second: with a real Lean grammar they cost */
		int diff = 0;
		for (size_t i = 0; i < n; i += 256) {
			size_t z = i + 256 < n ? i + 256 : n;
			syntax_faces(e.buf->syn, i, z, g + i);
			lsp_faces(e.buf, i, z, f + i);
		}
		diff = memcmp(g, f, n) != 0;
		if (diff && !first) { first = now() - t0; fprintf(stderr, "tokens first seen after %.0f ms\n", first); }
	}
	fprintf(stderr, "longest loop turn beyond its 5 ms wait: %.2f ms\n", worst);
	for (size_t i = 0; i < n; i++) printf("%d %d\n", g[i], f[i]);
	core_free(&e);
	return 0;
}
