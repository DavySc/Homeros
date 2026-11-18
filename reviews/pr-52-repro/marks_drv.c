/* Review of PR 52: tests/conform_drv.c, also printing the Visual marks (the
   lower one first, as getpos("'<") and getpos("'>") give them) and the
   change list.  Built against package 5's keys (PR 26) and this PR's core:
   see keys-tree.sh. */
#include <stdio.h>
#include "core.h"

static void mark(const char *name, Mark m)
{
	if (m.set) printf(", \"%s\": [%zu, %zu]", name, m.line + 1, m.col + 1);
	else printf(", \"%s\": [0, 0]", name);
}

int main(int argc, char **argv)
{
	static Editor e;
	int k, next;
	if (argc != 2 || core_open(&e, argv[1])) return 2;
	for (int more = scanf("%d", &k) == 1; more; k = next) {
		more = scanf("%d", &next) == 1;
		keys_feed(&e, k, more);
	}
	while (e.mode == MODE_INSERT || e.ks.pending) keys_feed(&e, KEY_ESC, 0);
	printf("{\"lines\": [");
	for (size_t l = 0; l < core_lines(&e); l++) {
		size_t n;
		const unsigned char *s = core_line(&e, l, &n);
		printf(l ? ", \"" : "\"");
		for (size_t i = 0; i < n; i++)
			if (s[i] == '"' || s[i] == '\\') printf("\\%c", s[i]);
			else if (s[i] < 0x20) printf("\\u%04x", s[i]);
			else putchar(s[i]);
		putchar('"');
	}
	printf("], \"cursor\": [%zu, %zu], \"mode\": %d", e.win->line + 1, e.win->col, e.mode);
	Mark a = e.buf->mark[MARK_LT], z = e.buf->mark[MARK_GT];
	if (a.set && z.set && (z.line < a.line || (z.line == a.line && z.col < a.col))) { Mark t = a; a = z; z = t; }
	mark("lt", a);
	mark("gt", z);
	printf("}\n");
	core_free(&e);
	return 0;
}
