/* PR 27 review: the real client (src/lsp, src/loop) against a real server.
   usage: live FILE SECONDS [server argv...]   (default: lean --server)
   Prints what lsp_diags shows at the end, with the bytes each range covers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"
int core_changed;
static long long now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000; }
int main(int argc, char **argv)
{
	static const char *av[8] = { "lean", "--server", NULL };
	static const char *const roots[] = { "lakefile.toml", NULL };
	if (argc > 3) { int k = 0; for (; k < 7 && argv[3 + k]; k++) av[k] = argv[3 + k]; av[k] = NULL; }
	const char *dot = strrchr(argv[1], '.');
	LspServer tab[2] = { { dot, !strcmp(dot, ".lean") ? "lean4" : "c", av, roots, !strcmp(dot, ".lean") }, { 0 } };
	lsp_servers = tab;
	FILE *f = fopen(argv[1], "rb");
	static char buf[1 << 22];
	size_t n = fread(buf, 1, sizeof buf, f);
	Text *t = text_new(buf, n);
	LspDoc *d = lsp_open(argv[1], t);
	if (!d) { fprintf(stderr, "no server\n"); return 2; }
	long long end = now() + atol(argv[2]) * 1000, quiet = 0;
	int seen = 0;
	while (now() < end) {
		loop_once(100);
		int np; lsp_progress(d, &np);
		if (core_changed) { core_changed = 0; seen = 1; quiet = now(); }
		if (seen && !np && now() - quiet > 1500) break;
	}
	int nd; const LspDiag *g = lsp_diags(d, &nd);
	printf("%d diagnostics\n", nd);
	for (int i = 0; i < nd; i++) {
		char b[64]; size_t m = g[i].z - g[i].a < 60 ? g[i].z - g[i].a : 60;
		text_read(t, g[i].a, b, m); b[m] = 0;
		char *nl = g[i].msg ? strchr(g[i].msg, '\n') : NULL; if (nl) *nl = 0;
		printf("  [%zu,%zu) sev %d \"%s\": %.70s\n", g[i].a, g[i].z, g[i].sev, b, g[i].msg ? g[i].msg : "(null)");
	}
	lsp_close(d);
	for (int k = 0; k < 20; k++) loop_once(50);
	return 0;
}
