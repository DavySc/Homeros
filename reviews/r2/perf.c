/* Review of package 2: what the undo file costs on a large text (with
   ref/text.c, the array).  argv: text MB, changes.  Times undo_write and
   undo_read; the read hashes the text, copies it and replays every change
   on the copy to check it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "undo.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
	size_t n = (size_t)atol(argv[1]) << 20; int changes = atoi(argv[2]);
	char *b = malloc(n); for (size_t i = 0; i < n; i++) b[i] = i % 61 ? 'a' + (char)(i % 26) : '\n';
	Text *t = text_new(b, n); free(b);
	Undo *u = undo_new();
	unsigned long long r = 88172645463325252ULL;
	for (int i = 0; i < changes; i++) {
		r ^= r << 13; r ^= r >> 7; r ^= r << 17;
		size_t pos = r % (text_len(t) + 1);
		if (text_insert(t, pos, "x", 1) || undo_record(u, pos, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0)) abort();
		undo_close(u);
	}
	char path[] = "/tmp/r2-perf-XXXXXX"; close(mkstemp(path));
	double a = now(); if (undo_write(u, t, path)) abort();
	double c = now(); Undo *v = undo_read(t, path); double d = now();
	if (!v) abort();
	printf("%s MB, %d changes: write %.3f s, read %.3f s\n", argv[1], changes, c - a, d - c);
	undo_free(u); undo_free(v); text_free(t); remove(path);
	return 0;
}
