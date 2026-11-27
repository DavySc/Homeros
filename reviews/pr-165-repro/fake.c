/* PR 165 review: keys over the test's fake server (tests/test_lsp_locs.py
   --serve CASES) on case 0, timed.  usage: fake CASES DIR KEYS...
   (DIR/0/m.c and the case's other files exist; DIR/lsproot too) */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "tools.h"

static Editor e;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	static char py[PATH_MAX], txt[PATH_MAX], file[PATH_MAX + 64];
	if (argc < 4 || !realpath("tests/test_lsp_locs.py", py) || !realpath(argv[1], txt)) return 2;
	static const char *const roots[] = { "lsproot", NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, "/tmp/claude-165-fake.log", NULL };
	LspServer table[2] = { { ".c", "c", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	snprintf(file, sizeof file, "%s/0/m.c", argv[2]);
	if (core_open(&e, file) || !e.buf->lsp) { puts("no server"); return 2; }
	int n = 0;
	for (int t = 0; t < 500; t++) { loop_once(10); lsp_diags(e.buf->lsp, &n); }
	printf("%d diagnostics, %zu lines\n", n, text_lines(e.buf->text));
	for (int i = 3; i < argc; i++) {
		const char *k = argv[i];
		if (*k == '@') { size_t l, c; sscanf(k + 1, "%zu,%zu", &l, &c); core_goto(&e, l, c); continue; }
		e.msg[0] = 0;
		double t0 = now();
		for (const char *c = k; *c; c++) keys_input(&e, (unsigned char)*c, c[1] != 0);
		double t1 = now();
		for (int t = 0; t < 50; t++) loop_once(10);
		const char *p = e.buf->path ? strrchr(e.buf->path, '/') : NULL;
		printf("%-6s %8.1f ms  -> %s %zu,%zu  %s\n", *k < 32 ? (*k == 29 ? "C-]" : *k == 15 ? "C-o" : "ctl") : k, t1 - t0, p ? p + 1 : "(list)", e.win->line, e.win->col, e.msg);
	}
	core_free(&e);
	return 0;
}
