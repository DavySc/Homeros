/* Review of 7dc6acd (:bd and the buffer's language server document): a
   buffer deleted, entered again by number (a new document), deleted again,
   then core_free; and two buffers with documents, one deleted.  Run under
   ASan with leak detection: any double close or leak ends it. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "lsp.h"
int main(void)
{
	static const char *const cat[] = { "sh", "-c", "cat >/dev/null", NULL }, *const none[] = { NULL };
	const LspServer srv[] = { { ".lspt", "lspt", cat, none, 0 }, { 0 } };
	static Editor e;
	const char *f[3] = { "/tmp/pr106-a.lspt", "/tmp/pr106-b.lspt", "/tmp/pr106-c.txt" };
	for (int i = 0; i < 3; i++) { FILE *h = fopen(f[i], "w"); fputs("x\n", h); fclose(h); }
	lsp_servers = srv;
	if (core_open(&e, f[2]) || core_edit_file(&e, f[0], 0) || !e.buf->lsp) return 1;
	int nr = e.buf->nr;
	if (core_buf_delete(&e, 0, 0) || strcmp(e.buf->path, f[2])) return 2;
	if (core_buf_goto(&e, nr) || strcmp(e.buf->path, f[0])) return 3;
	printf("entered again: document %s\n", e.buf->lsp ? "open" : "NONE");
	if (core_edit_file(&e, f[1], 0) || !e.buf->lsp) return 4;
	if (core_buf_delete(&e, nr, 0)) return 5;
	if (core_buf_delete(&e, nr, 0)) printf("second :bd of the same buffer refused\n");
	core_free(&e);
	for (int i = 0; i < 3; i++) unlink(f[i]);
	printf("ok\n");
	return 0;
}
