/* Second review of PR 20: the per-buffer cap of 1,000 undo-mark entries
   (UMARKS_MAX) against the undo tree's 1,000 levels.  Mark a is moved before
   every group; after undoing group g it must be where it was when g opened.
   Run 1: N groups in a row, undone as far as undo goes.  Run 2: 600 groups,
   500 undone, 600 new ones (a second branch), then undone as far as it goes.
   Prints the groups undone and the marks that came back wrong. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
static int want[5000], top;
static int group(Editor *e)
{
	static int n;
	e->buf->mark[0] = (Mark){ 0, (size_t)(++n % 3), 1 };
	want[++top] = n % 3;
	e->win->line = 0; e->win->col = 0;
	size_t len; core_line(e, 0, &len);
	if (core_edit(e, len, 0, "z", 1)) return -1;
	undo_close(e->buf->undo);
	return 0;
}
static void unwind(Editor *e, int max, const char *what)
{
	int done = 0, bad = 0;
	while (done < max && core_undo(e, 0) >= 0) {
		done++;
		if (!e->buf->mark[0].set || (int)e->buf->mark[0].col != want[top]) bad++;
		top--;
	}
	printf("%s: %d groups undone, %d marks wrong\n", what, done, bad);
}
int main(void)
{
	char path[] = "/tmp/markscapXXXXXX";
	if (mkstemp(path) < 0) return 2;
	FILE *f = fopen(path, "w"); fputs("abc\n", f); fclose(f);
	static Editor e;
	if (core_open(&e, path)) return 2;
	for (int i = 0; i < 1100; i++) if (group(&e)) return 2;
	unwind(&e, 5000, "1,100 groups in a row");
	core_free(&e);
	if (core_open(&e, path)) return 2;
	top = 0;
	for (int i = 0; i < 600; i++) if (group(&e)) return 2;
	unwind(&e, 500, "600 groups, 500 undone");
	for (int i = 0; i < 600; i++) if (group(&e)) return 2;
	unwind(&e, 5000, "then 600 on a new branch");
	core_free(&e); unlink(path);
	return 0;
}
