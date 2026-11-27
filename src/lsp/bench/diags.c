/* Homeros's side of diags.py: opens FILE.c with test_lsp_locs.py's fake
   server publishing TRACE's diagnostics, then from the middle line types
   each key, printing its ms and the line it went to.
   diags TRACE FILE.c N, from the repo; build with the editor's sources
   and -O2, as the Makefile's test programs but without the sanitizers */
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"

int main(int argc, char **argv)
{
	static Editor e;
	static char py[PATH_MAX], txt[PATH_MAX], dir[] = "/tmp/diagsXXXXXX", file[PATH_MAX + 64];
	CHECK(argc == 4 && realpath("tests/test_lsp_locs.py", py) && realpath(argv[1], txt) && mkdtemp(dir));
	static const char *const roots[] = { "lsproot", NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, NULL };
	LspServer table[2] = { { ".c", "c", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	snprintf(file, sizeof file, "%s/lsproot", dir);
	FILE *o = fopen(file, "w");
	CHECK(o && !fclose(o));
	snprintf(file, sizeof file, "%s/0", dir);
	CHECK(!mkdir(file, 0700));
	snprintf(file, sizeof file, "%s/0/m.c", dir);
	CHECK(!link(argv[2], file) && !core_open(&e, file) && e.buf->lsp);
	int want = atoi(argv[3]), have = 0;
	for (int t = 0; t < 3000 && have < want; t++) { loop_once(10); lsp_diags(e.buf->lsp, &have); }
	CHECK(have == want);
	static const char *const keys[] = { "]d", "[d", "]D", "[D", "9]d" };
	for (int i = 0; i < 5; i++) {
		core_goto(&e, text_line_of(e.buf->text, text_len(e.buf->text)) / 2, 0);
		struct timespec a, z;
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (const char *c = keys[i]; *c; c++) keys_input(&e, (unsigned char)*c, c[1] != 0);
		clock_gettime(CLOCK_MONOTONIC, &z);
		printf("%.3f %zu\n", (z.tv_sec - a.tv_sec) * 1e3 + (z.tv_nsec - a.tv_nsec) / 1e6, e.win->line);
	}
	return 0;
}
