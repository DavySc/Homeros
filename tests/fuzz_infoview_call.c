/* libFuzzer: the editor's side of the pipe (src/infoview/infoview.c,
   included whole) on any line the child could pass on from a page, with a
   Lean buffer open (its server a process that never answers), so the
   page's edits and jumps reach the core.  The byte 0x01 in the input
   stands for the buffer's URI.  From the review of PR 43, where it found
   two undefined behaviours in 20 s (reviews/pr-43-repro/). */
#include <stdint.h>
#include <sys/stat.h>
/* its entry points are linked from the library too */
#define bridge_open fuzz_bridge_open
#define bridge_update fuzz_bridge_update
#define bridge_free fuzz_bridge_free
#include "../src/infoview/infoview.c"

static const char text0[] = "theorem t : True := by\n  trivial\n-- \xCE\xB1\xF0\x9D\x94\xB8 x\n";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
	static Editor e;
	static int set;
	static const char *const roots[] = { "lean-toolchain", NULL };
	static const char *const argv[] = { "sleep", "1000000", NULL };
	static const LspServer table[] = { { ".lean", "lean4", argv, roots, 1 }, { NULL, NULL, NULL, NULL, 0 } };
	static char dir[64], path[128];
	if (!set) {
		char p[128];
		strcpy(dir, "/tmp/homeros-ivcall-XXXXXX");
		if (!mkdtemp(dir)) abort();
		snprintf(p, sizeof p, "%s/lean-toolchain", dir);
		FILE *f = fopen(p, "w"); fputs("x\n", f); fclose(f);
		snprintf(path, sizeof path, "%s/A.lean", dir);
		f = fopen(path, "w"); fputs(text0, f); fclose(f);
		lsp_servers = table;
		if (core_open(&e, path) || !e.buf->lsp) abort();
		ed = &e;
		set = 1;
	}
	const char *uri = lsp_uri(e.buf->lsp);
	Str s = { 0 };
	for (size_t i = 0; i < n; i++)
		if (data[i] == 1) j_puts(&s, uri);
		else if (data[i] == '\n' || data[i] == 0) j_put(&s, " ", 1);   /* a line holds neither */
		else j_put(&s, data + i, 1);
	j_put(&s, "", 0);
	from_child(s.b, s.n);
	free(s.b);
	/* afresh: no pages, the buffer as it was, the first buffer current */
	while (pages) drop(pages);
	if (e.buf->nr != 1) core_buf_goto(&e, 1);
	if (core_edit(&e, 0, text_len(e.buf->text), text0, sizeof text0 - 1)) abort();
	e.win->line = e.win->col = 0;
	return 0;
}
