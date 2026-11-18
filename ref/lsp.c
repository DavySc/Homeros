/* The thin editor has no language servers (include/lsp.h): no file gets a
   document, so every call has none to act on. */
#include "lsp.h"

LspDoc *lsp_open(const char *path, const Text *t) { (void)path; (void)t; return NULL; }
void lsp_close(LspDoc *d) { (void)d; }
void lsp_edit(LspDoc *d, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	(void)d; (void)t; (void)pos; (void)ndel; (void)ins; (void)nins;
}
void lsp_flush(LspDoc *d) { (void)d; }
void (*lsp_tap)(int out, const char *msg, size_t n);
int lsp_send(const char *uri, const char *method, const char *params, size_t n, LspRawFn fn, void *ctx)
{
	(void)uri; (void)method; (void)params; (void)n; (void)fn; (void)ctx;
	return -1;
}
const char *lsp_uri(const LspDoc *d) { (void)d; return NULL; }
const char *lsp_init(const LspDoc *d, size_t *n) { (void)d; *n = 0; return NULL; }
