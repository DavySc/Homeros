/* The thin editor has no language servers (include/lsp.h): no file gets a
   document, so every call has none to act on. */
#include "lsp.h"

int lsp_dirty;
LspDoc *lsp_open(const char *path, const Text *t) { (void)path; (void)t; return NULL; }
void lsp_close(LspDoc *d) { (void)d; }
void lsp_edit(LspDoc *d, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	(void)d; (void)t; (void)pos; (void)ndel; (void)ins; (void)nins;
}
void lsp_flush(LspDoc *d) { (void)d; }
