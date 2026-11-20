/* PR 27 review: libFuzzer on what the server may send (src/lsp/lsp.c's out
   and handle), with a document open and edits logged.  The first byte picks
   raw bytes to the pipe or one framed message. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"
int core_changed;
static ProcFn sfn; static void *sctx;
int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx) { (void)argv; (void)dir; sfn = fn; sctx = ctx; return 1; }
int proc_write(int id, const void *b, size_t n) { (void)id; (void)b; (void)n; return 0; }
void proc_close(int id) { (void)id; }
static void feed(const char *m, size_t len)
{
	char *all = malloc(len + 64);
	int n = snprintf(all, 64, "Content-Length: %zu\r\n\r\n", len);
	memcpy(all + n, m, len);
	sfn(sctx, 1, all, (size_t)n + len);
	free(all);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char *const av[] = { "x", NULL };
	static const LspServer tab[] = { { ".c", "c", av, av, 1 }, { 0 } };
	static const char s[] = "ab \xF0\x9F\x98\x80 cd\n\xE2\x88\x80 x\n\nlast";
	lsp_servers = tab;
	Text *t = text_new(s, sizeof s - 1);
	LspDoc *d = lsp_open("/x.c", t);
	const char *init = "{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{}}";
	feed(init, strlen(init));
	lsp_edit(d, t, 3, 4, "q\n", 2); text_delete(t, 3, 4); text_insert(t, 3, "q\n", 2);
	lsp_flush(d);
	lsp_ask(d, LSP_HOVER, 2); lsp_ask(d, LSP_GOAL, 5);
	lsp_edit(d, t, 0, 0, "Z", 1); text_insert(t, 0, "Z", 1);
	if (size) {
		if (data[0] & 1) sfn(sctx, 1, (const char *)data + 1, size - 1);
		else feed((const char *)data + 1, size - 1);
	}
	int n; const LspDiag *g = lsp_diags(d, &n);
	for (int i = 0; i < n; i++) if (g[i].a > g[i].z || g[i].z > text_len(t)) abort();
	g = lsp_progress(d, &n);
	for (int i = 0; i < n; i++) if (g[i].a > g[i].z || g[i].z > text_len(t)) abort();
	(void)lsp_answer(d, LSP_HOVER); (void)lsp_answer(d, LSP_GOAL);
	lsp_edit(d, t, 1, 2, "", 0); text_delete(t, 1, 2);
	g = lsp_diags(d, &n);
	for (int i = 0; i < n; i++) if (g[i].a > g[i].z || g[i].z > text_len(t)) abort();
	lsp_close(d);
	sfn(sctx, 0, NULL, 0);          /* the server exits: it is freed */
	text_free(t);
	return 0;
}
