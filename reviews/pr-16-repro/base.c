/* Finding 7: the first change after an open or a save writes the whole
   buffer to the journal inside the command.  Times that first journal_base
   for a text of argv[2] MiB, 5 times (a save between: journal_saved).
   build: gcc -O2 -Iinclude -o base base.c src/file/*.c src/text/text.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	size_t n = (size_t)atoi(argv[2]) << 20;
	char *s = malloc(n);
	for (size_t i = 0; i < n; i++) s[i] = i % 64 == 63 ? '\n' : 'a' + (char)(i % 26);
	Text *t = text_new(s, n), *rt;
	Journal *j; unsigned st; FileId id = { 0 }, rid;
	if (argc < 3 || journal_open(argv[1], &j, &rt, &st, &rid)) return 1;
	printf("%4d MiB:", atoi(argv[2]));
	for (int i = 0; i < 5; i++) {
		double a = now();
		journal_base(j, t, 0, &id);
		journal_edit(j, 0, 0, "x", 1, 0);
		double b = now();
		journal_sync(j);
		printf("  %.1f ms (+%.1f sync)", b - a, now() - b);
		journal_saved(j);
	}
	puts("");
	journal_close(j);
	return 0;
}
