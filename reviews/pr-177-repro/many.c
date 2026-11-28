/* many N: N files on one server, each with a first (partial) token answer;
   then the server sends one workspace/semanticTokens/refresh.  Prints how
   many files show the second word coloured a second later (the refreshed
   answer), and whether another timer could be set meanwhile. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "text.h"
static void other(void *x) { (void)x; }
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 20, full = 0, part = 0, other_ok = -2;
	static const char *sargv[] = { "python3", NULL, NULL, NULL };
	static char *const roots[] = { NULL };
	static LspServer table[2];
	char fake[1024];
	snprintf(fake, sizeof fake, "%s/many.py", getenv("REPRO"));
	sargv[1] = fake; sargv[2] = argv[1];
	table[0] = (LspServer){ ".fake", "fake", sargv, (const char *const *)roots, 0 };
	lsp_servers = table;
	Buf *b = calloc((size_t)n, sizeof *b);
	char path[256];
	for (int i = 0; i < n; i++) {
		snprintf(path, sizeof path, "/tmp/many-%d-%d.fake", (int)getpid(), i);
		FILE *f = fopen(path, "w"); fputs("x y\n", f); fclose(f);
		b[i].text = text_new("x y\n", 4);
		if (!(b[i].lsp = lsp_open(path, b[i].text))) return 1;
	}
	for (long long t0 = loop_now(); loop_now() - t0 < 1500000000LL; ) {
		loop_once(10);
		unsigned char f[4] = { 0 };
		int p = 0;
		for (int i = 0; i < n; i++) { lsp_faces(&b[i], 0, 4, f); p += f[0] != 0; }
		/* the first answers are in and the refresh has been handled: is the table full? */
		if (p == n && other_ok == -2) { loop_once(50); other_ok = loop_timer(other, NULL, 1); }
	}
	for (int i = 0; i < n; i++) {
		unsigned char f[4] = { 0 };
		lsp_faces(&b[i], 0, 4, f);
		part += f[0] != 0; full += f[2] != 0;
	}
	/* $MANY_EDIT: the text deleted byte by byte from the front and typed
	   again, the faces asked after each step (tokens kept as points) */
	for (int k = 0; getenv("MANY_EDIT") && k < 8; k++) {
		unsigned char f[8] = { 0 };
		if (k < 4) { lsp_edit(b[0].lsp, b[0].text, 0, 1, "", 0); text_delete(b[0].text, 0, 1); }
		else { lsp_edit(b[0].lsp, b[0].text, 0, 0, "x y\n" + k - 4, 1); text_insert(b[0].text, (size_t)k - 4, "x y\n" + k - 4, 1); }
		lsp_flush(b[0].lsp);
		lsp_faces(&b[0], 0, text_len(b[0].text), f);
		loop_once(10);
	}
	printf("%d files: first word coloured in %d, second (after the refresh) in %d; another loop_timer just after the refresh: %d\n", n, part, full, other_ok);
	for (int i = 0; i < n; i++) {
		lsp_close(b[i].lsp);
		snprintf(path, sizeof path, "/tmp/many-%d-%d.fake", (int)getpid(), i);
		unlink(path);
	}
	return 0;
}
