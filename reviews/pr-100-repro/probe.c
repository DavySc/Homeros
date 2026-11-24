/* PR 100 review: cit/ciT cases beside test_keys_map's.  Each case: a file's
   text, keys, then the text and cursor column are printed.
   Build as tests/test_keys_map.c is built (see run.sh). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_input(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

static void run(const char *text, const char *keys)
{
	static Editor e;
	char org[] = "/tmp/pr100XXXXXX.org", b[512];
	int fd = mkstemps(org, 4);
	CHECK(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text) && !close(fd));
	CHECK(!core_open(&e, org));
	feed(&e, keys);
	size_t n = text_read(e.buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	printf("keys [");
	for (const char *k = keys; *k; k++) *k == 27 ? printf("<Esc>") : putchar(*k);
	printf("]\n  text [");
	for (char *p = b; *p; p++) *p == '\n' ? printf("\\n") : putchar(*p);
	printf("] line %zu col %zu mode %d pending %d msg [%s]\n", e.win->line, e.win->col, e.mode, e.ks.pending, e.msg);
	core_free(&e);
	unlink(org);
}

int main(int argc, char **argv)
{
	if (argc == 3) { run(argv[1], argv[2]); return 0; }
	run("* a b\n", "xcitu");          /* u: only cit undone?  want [*  b] */
	run("* a b\n", "citxu");          /* want [* TODO a b] */
	run("* a b\n", "citcitu");        /* want [* TODO a b] */
	run("* a b\n", "citcitcituu");    /* want [* TODO a b] */
	run("* a b\n", "ixy\033citu");    /* insert then cit: u keeps xy */
	run("* a b\n", "ddcitu");         /* dd then cit on an empty buffer */
	run("* a b\n", "2cit");           /* a count */
	run("* a b\n", "\"acit");         /* a register */
	run("* a b\n", "dcit");           /* operator pending: d then c is nothing, i t ... */
	run("* a b\n", "vcit");           /* Visual c */
	run("* a b\nxx <i>q</i>\n", "j0fqcitZ\033");  /* cit on a plain line of an entry */
	run("* a b\n", "ciwZ\033");       /* c i then no map: ciw */
	run("* a b\n", "ccZ\033");
	run("* a b\n", "c\033x");         /* c Esc */
	run("* a b\n", "ci\033x");
	run("* a b\n", "cit.");           /* dot after cit */
	run("* a b\n", "qqcitq@q");       /* recorded */
	run("* a b\n", "qqcwZ\033q0w@q"); /* cw recorded through the wait */
	run("* a b\n", "cwZ\0330.");      /* cw, then dot */
	run("text\n", "cit");             /* before the first headline */
	run("", "cit");
	run("* DONE a\n", "cit");
	run("* a b\n", ":s/a/q/\rcitu");
	return 0;
}
