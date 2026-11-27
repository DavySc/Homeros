/* Review of PR 120: the time of one core_draw of a 40x120 window over a
   buffer with many diagnostics (case file from gen_many.py; the fake
   server is tests/test_core_diag.py --serve), at the top and at the end.
   usage: diag_bench CASEFILE NDIAGS TEXTFILE */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "../../src/core/core_int.h"

int main(int argc, char **argv)
{
	static Editor e;
	static char py[PATH_MAX], txt[PATH_MAX];
	if (argc < 4 || !realpath("tests/test_core_diag.py", py) || !realpath(argv[1], txt)) return 2;
	static const char *const roots[] = { NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, NULL };
	LspServer table[2] = { { ".diag", "diag", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	int nd = atoi(argv[2]), have = 0;
	Grid g = { 0 };
	if (core_open(&e, argv[3]) || grid_resize(&g, 40, 120)) return 2;
	for (int t = 0; t < 3000 && have < nd; t++) { loop_once(10); lsp_diags(e.buf->lsp, &have); }
	layout_resize(&e, 40, 120);
	for (int where = 0; where < 2; where++) {
		e.win->line = where ? text_lines(e.buf->text) - 1 : 0;
		e.win->col = 0;
		core_update(&e);
		core_draw(&e, &g);
		long long best = -1;
		for (int k = 0; k < 20; k++) {
			long long t0 = loop_now();
			core_draw(&e, &g);
			long long d = loop_now() - t0;
			if (best < 0 || d < best) best = d;
		}
		int under = 0;
		for (int i = 0; i < g.rows * g.cols; i++) under += g.c[i].ul != 0;
		printf("%d diagnostics, cursor on line %zu: core_draw best of 20 = %.3f ms, %d cells underlined\n",
		       have, e.win->line, (double)best / 1e6, under);
	}
	core_free(&e);
	return 0;
}
