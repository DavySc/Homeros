/* Finding 1: after a failed journal append (disk full, here a file-size
   limit), later appends succeed, so the journal has a hole and replay gives a
   text the user never had.
   build: gcc -Iinclude -o gap reviews/pr-16-repro/gap.c src/file/*.c src/text/text.c
   run:   export XDG_STATE_HOME=$(mktemp -d); touch /tmp/f; ./gap /tmp/f; ./gap2 /tmp/f   (add "low" before the path for the wrong-text variant) */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "file.h"

static void show(const char *what, Text *t)
{
	char b[256];
	size_t n = text_read(t, 0, b, sizeof b - 1);
	b[n] = 0;
	printf("%s: \"%s\"\n", what, b);
}

int main(int argc, char **argv)
{
	const char *path = argv[argc - 1];   /* an existing file */
	Journal *j;
	Text *t = text_new("0123456789", 10), *rt;
	unsigned st;
	FileId id = { 0 }, rid;
	signal(SIGXFSZ, SIG_IGN);
	if (journal_open(path, &j, &rt, &st, &rid)) { perror("open"); return 1; }
	journal_base(j, t, 0, &id);
	/* room for a small record, not for a large one */
	struct rlimit rl = { 200, RLIM_INFINITY }, no = { RLIM_INFINITY, RLIM_INFINITY };
	setrlimit(RLIMIT_FSIZE, &rl);
	char big[100];
	memset(big, 'X', sizeof big);
	int r1 = journal_edit(j, 0, 0, big, sizeof big, 0);   /* the paste: fails */
	text_insert(t, 0, big, sizeof big);                   /* the core goes on (E297) */
	size_t at = argc > 2 ? 2 : 105;                       /* "low": inside the old text too */
	int r2 = journal_edit(j, at, 5, "end", 3, 0);         /* a later small edit fits */
	text_delete(t, at, 5); text_insert(t, at, "end", 3);
	setrlimit(RLIMIT_FSIZE, &no);
	printf("paste journalled: %d, later edit journalled: %d\n", r1, r2);
	show("buffer at the crash", t);
	fflush(stdout);
	_exit(0);   /* crash: the journal stays */
}
