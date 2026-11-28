/* PR 165 review: the keys on real clangd, the file opened by a relative
   name (as `homeros m.c` does).  usage: real DIR   (DIR holds m.c a.h a.c) */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "tools.h"

static Editor e;
static void spin(int ms) { for (int t = 0; t < ms / 10; t++) loop_once(10); }
static void type(const char *k)
{
	e.msg[0] = 0;
	for (const char *c = k; *c; c++) keys_input(&e, (unsigned char)*c, c[1] != 0);
	spin(1500);
}
static void where(const char *what)
{
	printf("%-28s %s %zu,%zu  msg='%s'\n", what, e.buf->path ? e.buf->path : "(none)", e.win->line, e.win->col, e.msg);
}
static void dump(void)
{
	size_t n = text_len(e.buf->text);
	char b[4096];
	if (n > sizeof b - 1) n = sizeof b - 1;
	text_read(e.buf->text, 0, b, n);
	b[n] = 0;
	printf("--- buffer ---\n%s--------------\n", b);
}
int main(int argc, char **argv)
{
	if (argc < 2 || chdir(argv[1])) return 2;
	if (core_open(&e, argc > 2 ? argv[2] : "m.c") || !e.buf->lsp) { puts("no server"); return 2; }
	spin(4000);
	int n; lsp_diags(e.buf->lsp, &n);
	printf("diagnostics: %d\n", n);
	core_goto(&e, 2, 25);
	where("start");
	type("\x1d"); where("C-] on twice (same file)");
	type("\x0f"); where("C-o");
	printf("tools_make: %d\n", tools_make(&e, "echo made")); spin(500); where("after :make");
	core_goto(&e, 1, 12);
	type("grr"); where("grr on twice"); dump();
	printf("nomodifiable %d, windows:", e.buf->nomodifiable);
	for (Win *w = e.wins; w; w = w->next) printf(" [%s%s]", w->buf->path ? w->buf->path : "(none)", w == e.win ? " *" : "");
	puts("");
	core_free(&e);
	return 0;
}
