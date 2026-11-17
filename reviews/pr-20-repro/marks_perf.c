/* Review of PR 20: the cost of one edit (core_edit, one undo group each)
   after GROUPS groups, with no mark set and with mark a set.  With a mark
   set every group keeps an entry in core.c's umarks list, and each new
   group walks the whole list (umarks_find).
   usage: marks_perf GROUPS MARK(0|1) ; prints the mean of the last 1000 edits in microseconds */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
int main(int argc, char **argv)
{
	long groups = argc > 1 ? atol(argv[1]) : 20000;
	int mark = argc > 2 && atoi(argv[2]);
	char path[] = "/tmp/marksperfXXXXXX";
	if (mkstemp(path) < 0) return 1;
	FILE *f = fopen(path, "w");
	for (int i = 0; i < 1000; i++) fprintf(f, "line %d\n", i);
	fclose(f);
	static Editor e;
	if (core_open(&e, path)) return 1;
	if (mark) e.buf->mark[0] = (Mark){ 0, 0, 1 };
	double t0 = 0;
	for (long i = 0; i < groups; i++) {
		if (i == groups - 1000) t0 = now();
		core_edit(&e, core_line_start(&e, 500), 0, "x", 1);
		undo_close(e.buf->undo);
	}
	printf("%.2f\n", (now() - t0) / 1000);
	core_free(&e); unlink(path);
	return 0;
}
