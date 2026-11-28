/* shared by the PR 180 probes: an editor on DIR/m.c ("int x;\nint y;\nint foo(int x);\n")
   with fake.py as its server, a grid, and helpers */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
static Editor e;
static Grid g;
static char dir[] = "/tmp/pr180XXXXXX", logf[PATH_MAX];
static void put(const char *name, const char *s) { FILE *f = fopen(name, "w"); CHECK(f && fputs(s, f) >= 0 && !fclose(f)); }
static void spin(int n) { for (int i = 0; i < n; i++) loop_once(10); }
static void draw(void) { core_draw(&e, NULL, &g); }
static void show(const char *what)
{
	draw();
	printf("--- %s: pop %s", what, e.pop ? "" : "none");
	if (e.pop) printf("y %d x %d w %d h %d lines %zu", e.pop->y, e.pop->x, e.pop->w, e.pop->h, (size_t)0);
	printf(", msg [%s]\n", e.msg);
	for (int r = 0; r < g.rows; r++) {
		printf("  |");
		for (int c = 0; c < g.cols; c++) { const Cell *x = &g.c[r * g.cols + c]; if (x->len) fwrite(grid_text(x), 1, x->len, stdout); }
		printf("|\n");
	}
}
static void start(int rows, int cols)
{
	static char py[PATH_MAX];
	static const char *sargv[6];
	static const char *const roots[] = { "lsproot", NULL };
	static LspServer table[2];
	CHECK(realpath(getenv("FAKE"), py) && mkdtemp(dir) && !chdir(dir));
	snprintf(logf, sizeof logf, "%s/log", dir);
	sargv[0] = "python3", sargv[1] = py, sargv[2] = logf;
	table[0] = (LspServer){ ".c", "c", sargv, roots, 0 };
	lsp_servers = table;
	put("lsproot", ""); put("log", "");
	put("m.c", "int x;\nint y;\nint foo(int x);\n");
	put("o.c", "char o;\nchar p;\n");
	CHECK(!core_open(&e, "m.c") && !grid_resize(&g, rows, cols) && e.buf->lsp);
	draw();
	spin(50);
}
static inline void cat_log(void) { char c[PATH_MAX + 8]; snprintf(c, sizeof c, "cat %s", logf); fflush(stdout); if (system(c)) {} }
