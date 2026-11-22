/* What the LSP benches share (stale.c, sv.c): the text, one edit as a
   command (one didChange, as Helix sends), and sorting times. */
#include <stdlib.h>
#include "lsp.h"
#include "text.h"

static Text *t;

static void edit(LspDoc *d, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	lsp_edit(d, t, pos, ndel, ins, nins);
	text_delete(t, pos, ndel);
	text_insert(t, pos, ins, nins);
	lsp_flush(d);
}

static int cmpl(const void *a, const void *b) { long long x = *(const long long *)a, y = *(const long long *)b; return (x > y) - (x < y); }
