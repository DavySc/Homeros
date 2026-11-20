/* PR 27 review: how long the loop is held by one publication (handle() runs
   inside the loop, between keys).  usage: perf DIAGS EDITS_IN_LOG [stale]
   A 20,000-line text; EDITS_IN_LOG one-key commands with no publication
   in between; then one publication of DIAGS ranges, for the current
   version or (stale) for the version before all those edits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"
int core_changed;
static ProcFn sfn; static void *sctx;
int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx) { (void)argv; (void)dir; sfn = fn; sctx = ctx; return 1; }
int proc_write(int id, const void *b, size_t n) { (void)id; (void)b; (void)n; return 0; }
void proc_close(int id) { (void)id; }
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
int main(int argc, char **argv)
{
	int nd = atoi(argv[1]), ne = atoi(argv[2]), stale = argc > 3;
	static const char *const av[] = { "x", NULL };
	static const LspServer tab[] = { { ".c", "c", av, av, 0 }, { 0 } };
	lsp_servers = tab;
	enum { LINES = 20000 };
	char *s = malloc(LINES * 41 + 1);
	for (int l = 0; l < LINES; l++) sprintf(s + l * 41, "theorem t%05d : a + b = b + a := sorry \n", l);
	double r[5];
	for (int run = 0; run < 5; run++) {
		Text *t = text_new(s, LINES * 41);
		LspDoc *d = lsp_open("/x.c", t);
		const char *init = "Content-Length: 36\r\n\r\n{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{}}";
		sfn(sctx, 1, init, strlen(init));
		for (int k = 0; k < ne; k++) { size_t pos = 41 * 100 + (size_t)k; lsp_edit(d, t, pos, 0, "x", 1); text_insert(t, pos, "x", 1); lsp_flush(d); }
		char *m = malloc((size_t)nd * 160 + 400), *p = m + 64;
		p += sprintf(p, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///x.c\",\"version\":%d,\"diagnostics\":[", stale ? 0 : ne);
		for (int i = 0; i < nd; i++)
			p += sprintf(p, "%s{\"range\":{\"start\":{\"line\":%d,\"character\":8},\"end\":{\"line\":%d,\"character\":14}},\"severity\":2,\"message\":\"declaration uses 'sorry'\"}", i ? "," : "", 200 + i * (LINES - 300) / nd, 200 + i * (LINES - 300) / nd);
		p += sprintf(p, "]}}");
		char h[64]; int hn = sprintf(h, "Content-Length: %zu\r\n\r\n", (size_t)(p - (m + 64)));
		memcpy(m + 64 - hn, h, (size_t)hn);
		double a = now();
		sfn(sctx, 1, m + 64 - hn, (size_t)(p - (m + 64)) + (size_t)hn);
		r[run] = now() - a;
		int n; lsp_diags(d, &n);
		if (n != nd) { fprintf(stderr, "%d shown of %d\n", n, nd); return 1; }
		free(m); lsp_close(d); text_free(t);
	}
	qsort(r, 5, sizeof *r, cmpd);
	printf("%5d diagnostics, %4d edits in the log, %s: median %.2f ms (%.2f to %.2f), 5 runs\n", nd, ne, stale ? "for the version before them" : "for the current version", r[2], r[0], r[4]);
	return 0;
}
