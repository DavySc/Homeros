/* Until thread 0's header round lands (package-14/header-request.md): the
   declarations asked for in include/bridge.h and include/lsp.h.  Deleted
   with that merge. */
#ifndef INFOVIEW_PENDING_H
#define INFOVIEW_PENDING_H
#include <stddef.h>
#include "lsp.h"
struct Editor;
int  bridge_open(struct Editor *e);
void bridge_update(struct Editor *e);
void bridge_free(void);
extern void (*lsp_tap)(int out, const char *msg, size_t n);
typedef void (*LspRawFn)(void *ctx, const char *msg, size_t n);
int  lsp_send(const char *uri, const char *method, const char *params, size_t n, LspRawFn fn, void *ctx);
const char *lsp_uri(const LspDoc *d);
#endif
