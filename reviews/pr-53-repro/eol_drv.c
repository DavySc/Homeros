/* Review of PR 53: opens FILE through the core, prints the format, the read
   message and the lines (hex), feeds the keys given as numbers on stdin,
   writes with core_write and prints its message.  With "crash" as argv[2]
   it exits without closing after the keys (the journal stays), so a second
   run recovers; with "force" the write is :w!. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core.h"

static void dump(Editor *e, const char *tag)
{
	printf("%s ff=%s noeol=%d mod=%d msg=%s\n", tag, e->buf->dos ? "dos" : "unix", e->buf->noeol, core_modified(e->buf), e->msg);
	printf("%s lines", tag);
	for (size_t l = 0; l < core_lines(e); l++) {
		size_t n;
		const unsigned char *s = core_line(e, l, &n);
		printf(" ");
		for (size_t i = 0; i < n; i++) printf("%02x", s[i]);
		printf(".");
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	static Editor e;
	int k, next;
	if (argc < 2 || core_open(&e, argv[1])) return 2;
	dump(&e, "open");
	int any = 0;
	for (int more = scanf("%d", &k) == 1; more; k = next) {
		more = scanf("%d", &next) == 1;
		keys_feed(&e, k, more);
		any = 1;
	}
	if (any) dump(&e, "keys");
	if (argc > 2 && !strcmp(argv[2], "crash")) { fflush(stdout); _exit(0); }
	if (argc > 2 && !strcmp(argv[2], "nowrite")) { core_free(&e); return 0; }
	int r = core_write(&e, e.buf, argc > 2 && !strcmp(argv[2], "force"));   /* :w! */
	printf("write r=%d msg=%s\n", r, e.msg);
	core_free(&e);
	return 0;
}
