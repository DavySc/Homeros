/* The oracle's editor (not built by make; oracle/README.md): a Lean file
   in the core, its server through lsp, the bridge open, driven by lines
   on stdin:
     c L C   cursor to line L, byte C (as a key would), then bridge_update
     g L C   Lean's own goal there ($/lean/plainGoal through lsp_send),
             printed as "goal" then its text then a line "\x04"
     p       the buffer, then "\x04"
   The page itself is driven by oracle/oracle.mjs in Chromium. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bridge.h"
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "../scan.h"

static Editor e;
static char in[65536];
static size_t nin;

/* the answer's result.rendered, as lsp.c showed it for LSP_GOAL */
static void goal(void *ctx, const char *m, size_t n)
{
	(void)ctx;
	char *g = j_str(m, n, j_member(m, n, j_member(m, n, 0, "result"), "rendered"), NULL);
	printf("goal\n%s\n\x04\n", g ? g : "");
	fflush(stdout);
	free(g);
}

static void line(char *l)
{
	size_t L, C;
	if (sscanf(l, "c %zu %zu", &L, &C) == 2) {
		e.win->line = L; e.win->col = C;
		bridge_update(&e);
	} else if (sscanf(l, "g %zu %zu", &L, &C) == 2) {
		size_t n;
		const unsigned char *t = core_line(&e, L, &n);
		char p[160];
		int k = snprintf(p, sizeof p, "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%zu,\"character\":%zu}}",
		                 lsp_uri(e.buf->lsp), L, lsp_u16(t, C < n ? C : n));
		lsp_send(lsp_uri(e.buf->lsp), "$/lean/plainGoal", p, (size_t)k, goal, NULL);
	} else if (!strcmp(l, "p")) {
		for (size_t i = 0, n; i < core_lines(&e); i++) {
			const unsigned char *s = core_line(&e, i, &n);
			fwrite(s, 1, n, stdout);
			putchar('\n');
		}
		printf("\x04\n");
		fflush(stdout);
	}
}

static void on_in(void *ctx, int fd)
{
	(void)ctx;
	ssize_t r = read(fd, in + nin, sizeof in - nin - 1);
	if (r <= 0) exit(0);
	nin += (size_t)r;
	char *nl;
	while ((nl = memchr(in, '\n', nin))) {
		*nl = 0;
		line(in);
		nin -= (size_t)(nl + 1 - in);
		memmove(in, nl + 1, nin);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2 || core_open(&e, argv[1])) { fprintf(stderr, "usage: drive FILE.lean\n"); return 2; }
	if (bridge_open(&e)) { fprintf(stderr, "drive: %s\n", e.msg); return 1; }
	loop_watch(0, on_in, NULL);
	for (;;) loop_once(-1);
}
