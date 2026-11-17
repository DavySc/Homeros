/* second half of gap.c: reopen and show what is recovered.  argv[1] = path */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
int main(int argc, char **argv)
{
	Journal *j; Text *t; unsigned st; FileId id; char b[256];
	int r = journal_open(argv[argc - 1], &j, &t, &st, &id);
	if (r < 0) { printf("journal_open -1: %s\n", strerror(errno)); return 0; }
	if (!r) { puts("nothing recovered"); return 0; }
	size_t n = text_read(t, 0, b, sizeof b - 1); b[n] = 0;
	printf("recovered: \"%s\"\n", b);
	return 0;
}
