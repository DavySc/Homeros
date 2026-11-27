/* Review of PR 120: the core's rows after the keys of insert.lua (three
   lines, a diagnostic on each; the fake server is test_core_diag.py
   --serve on insert-case.txt).  usage: insert_probe CASEFILE TEXTFILE */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "../../src/core/core_int.h"

static Editor e;
static Grid g;

static void show(const char *what)
{
	core_update(&e);
	core_draw(&e, &g);
	printf("%s: ", what);
	for (int r = 0; r < 3; r++) {
		char b[512];
		size_t k = 0;
		for (int c = 0; c < g.cols; c++) {
			const Cell *x = &g.c[r * g.cols + c];
			memcpy(b + k, grid_text(x), x->len);
			k += x->len;
		}
		while (k && b[k - 1] == ' ') k--;
		printf("%.*s%s", (int)k, b, r < 2 ? " | " : "\n");
	}
}

static void feed(const int *k) { for (; *k; k++) keys_input(&e, *k, k[1] != 0); }

int main(int argc, char **argv)
{
	static char py[PATH_MAX], txt[PATH_MAX];
	if (argc < 3 || !realpath("tests/test_core_diag.py", py) || !realpath(argv[1], txt)) return 2;
	static const char *const roots[] = { NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, NULL };
	LspServer table[2] = { { ".diag", "diag", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	int have = 0;
	if (core_open(&e, argv[2]) || grid_resize(&g, 8, 50)) return 2;
	for (int t = 0; t < 500 && have < 3; t++) { loop_once(10); lsp_diags(e.buf->lsp, &have); }
	layout_resize(&e, 8, 50);
	feed((int[]){ 'g', 'g', 0 }); show("Normal, line 1");
	feed((int[]){ 'i', KEY_DOWN, 0 }); show("i<Down>");
	feed((int[]){ KEY_ESC, 0 }); show("<Esc>");
	feed((int[]){ 'g', 'g', 'v', 'j', 0 }); show("ggvj");
	feed((int[]){ KEY_ESC, 'j', 'j', 0 }); show("<Esc>jj");
	core_free(&e);
	return 0;
}
