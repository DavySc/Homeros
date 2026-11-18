/* Review of PR 20: an undo group's "join" flag outlives its buffer.
   edit_file() reuses an empty unnamed buffer through buf_unload(&old), and
   umarks_drop() compares the buffer's address, so it drops nothing: the
   entry (b, seq 1, join) stays and the new file's first undo group is read
   as a join.  The second half is tests/test_core_undo.c's "same bytes as dd"
   case, which expects line 1 after undo and redo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
static int dd_case(Editor *e)
{
	undo_close(e->buf->undo);
	e->win->line = 2; e->win->col = 0;
	if (core_edit(e, core_line_start(e, 2) - 1, 1, "", 0)) return -1;
	e->win->line = 1; e->win->col = 0;
	undo_close(e->buf->undo);
	if (core_undo(e, 0) < 0 || core_undo(e, 1) < 0) return -1;
	return (int)e->win->line;
}
int main(void)
{
	char path[] = "/tmp/stalejoinXXXXXX";
	if (mkstemp(path) < 0) return 2;
	FILE *f = fopen(path, "w"); fputs("x\n\n\ny\n", f); fclose(f);
	static Editor e;
	if (core_open(&e, path)) return 2;
	printf("fresh editor:                       line after undo, redo = %d\n", dd_case(&e));
	core_free(&e);
	/* an unnamed buffer: a line opened and joined back in one group, undone */
	if (core_open(&e, NULL)) return 2;
	if (core_edit(&e, 0, 0, "\n", 1)) return 2;
	core_mark_join(&e, 1, 0, -1, 0, 0);
	if (core_edit(&e, 0, 1, "", 0)) return 2;
	undo_close(e.buf->undo);
	if (core_undo(&e, 0) < 0) return 2;
	if (core_edit_file(&e, path, 0)) return 2;     /* reuses the empty buffer */
	int l = dd_case(&e);
	printf("after a join in the reused buffer:  line after undo, redo = %d%s\n", l, l == 1 ? "" : "   <-- differs");
	core_free(&e); unlink(path);
	return l != 1;
}
