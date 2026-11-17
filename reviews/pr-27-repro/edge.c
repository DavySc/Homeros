/* PR 27 review: single cases against the client with a stub loop.
   Each case: a text, optional edits, one publication; prints what is shown. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"
int core_changed;
static ProcFn sfn; static void *sctx;
static char sent[1 << 20]; static size_t nsent;
int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx) { (void)argv; (void)dir; sfn = fn; sctx = ctx; return 1; }
int proc_write(int id, const void *b, size_t n) { (void)id; if (nsent + n < sizeof sent) { memcpy(sent + nsent, b, n); nsent += n; sent[nsent] = 0; } return 0; }
void proc_close(int id) { (void)id; }
static void feed(const char *m)
{
	static char all[1 << 16];
	int n = snprintf(all, sizeof all, "Content-Length: %zu\r\n\r\n%s", strlen(m), m);
	sfn(sctx, 1, all, (size_t)n);
}
static Text *t; static LspDoc *d;
static void open(const char *s)
{
	static const char *const av[] = { "x", NULL };
	static const LspServer tab[] = { { ".c", "c", av, av, 0 }, { 0 } };
	lsp_servers = tab;
	t = text_new(s, strlen(s)); d = lsp_open("/x.c", t);
	feed("{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{}}");
}
static void edit(size_t pos, size_t ndel, const char *ins)
{
	lsp_edit(d, t, pos, ndel, ins, strlen(ins)); text_delete(t, pos, ndel); text_insert(t, pos, ins, strlen(ins));
}
static void pub(long v, const char *ranges)   /* ranges: "l,c,l,c;..." */
{
	char m[4096]; int n = sprintf(m, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///x.c\",\"version\":%ld,\"diagnostics\":[", v);
	int k = 0; long a, b, c, e; int used;
	while (sscanf(ranges, "%ld,%ld,%ld,%ld%n", &a, &b, &c, &e, &used) == 4) {
		n += sprintf(m + n, "%s{\"range\":{\"start\":{\"line\":%ld,\"character\":%ld},\"end\":{\"line\":%ld,\"character\":%ld}},\"severity\":1,\"message\":\"d%d\"}", k ? "," : "", a, b, c, e, k);
		k++; ranges += used; if (*ranges == ';') ranges++;
	}
	sprintf(m + n, "]}}");
	feed(m);
}
static void show(const char *what)
{
	int n; const LspDiag *g = lsp_diags(d, &n);
	char b[256]; size_t len = text_read(t, 0, b, 255); b[len] = 0;
	for (char *p = b; *p; p++) if (*p == '\n') *p = '$';
	printf("%s\n  text \"%s\"\n  shown %d:", what, b, n);
	for (int i = 0; i < n; i++) printf(" %s[%zu,%zu)", g[i].msg, g[i].a, g[i].z);
	printf("\n");
}
static void done(void) { lsp_close(d); text_free(t); nsent = 0; }
int main(void)
{
	open("abcde\nfgh\n");
	pub(0, "0,7,0,9;0,2,0,99;0,5,1,0;5,0,5,3;0,0,9,0");
	show("1. no edits; ranges past the line end, on the line break, past the last line");
	done();

	open("abcde\nfgh\n");
	edit(5, 0, "XY"); lsp_flush(d);
	pub(0, "0,2,0,99;0,2,0,5");
	show("2. v0 ranges [2, line end) sent as end column 99 and as 5; then XY typed at the line end (v1); the v0 publication arrives");
	done();

	open("abcde\nfgh\n");
	pub(0, "0,2,0,99;0,2,0,5");
	edit(5, 0, "XY"); lsp_flush(d);
	show("3. the same, publication first (shown ranges shifted)");
	done();

	/* a lone CR is a line end in LSP (spec: \n, \r\n and \r) */
	open("ab\rcd\nef\n");
	edit(4, 1, "X");
	lsp_flush(d);
	printf("4. text ab<CR>cd$ef$, 'd' (byte 4) replaced: sent %s\n", strstr(sent, "\"contentChanges\""));
	done();

	/* invalid UTF-8: what goes to the server */
	open("a\xE9" "b\n");
	edit(3, 0, "Z"); lsp_flush(d);
	printf("5. Latin-1 text a<E9>b: didOpen text bytes:");
	const char *tx = strstr(sent, "\"text\":\"");
	for (const unsigned char *p = (const unsigned char *)tx + 8; *p != '"'; p++) printf(" %02x", *p);
	printf("; change %s\n", strstr(sent, "\"contentChanges\""));
	done();
	return 0;
}
