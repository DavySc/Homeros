/* Review of PR 52, finding 3: snap_buf and snap_seq are one pair for all
   buffers.  A group left open in buffer A while buffer B is edited is taken
   for a new group at A's next edit, and its saved marks are overwritten
   with the marks as they are by then.  Core API only: package 5's keys
   close the group at every Normal mode key, so they do not reach this.
   Prints mark a after the undo: 2 is right (where it was when the group
   opened), and what main gives.
   Build: make -f Makefile -f reviews/pr-52-repro/drv.mk DRV=reviews/pr-52-repro/twobuf.c build/san/twobuf */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "core.h"
#include "undo.h"

int main(void)
{
	static Editor e;
	char pa[] = "/tmp/pr52aXXXXXX", pb[] = "/tmp/pr52bXXXXXX";
	FILE *f = fdopen(mkstemp(pa), "w");
	fputs("a\nb\nc\n", f);
	fclose(f);
	f = fdopen(mkstemp(pb), "w");
	fputs("x\n", f);
	fclose(f);
	if (core_open(&e, pa)) return 2;
	Buf *a = e.buf;
	int nr = a->nr;
	a->mark[0] = (Mark){ 1, 0, 1 };                  /* mark a on line 2 */
	undo_close(a->undo);
	if (core_edit(&e, 0, 0, "new\n", 4)) return 2;    /* a line above: a is on line 3, the group saved line 2 */
	if (core_edit_file(&e, pb, 0)) { printf("switch: %s\n", e.msg); return 2; }
	if (core_edit(&e, 0, 0, "y", 1)) return 2;
	undo_close(e.buf->undo);
	if (core_buf_goto(&e, nr)) { printf("back: %s\n", e.msg); return 2; }
	if (core_edit(&e, 0, 0, "z", 1)) return 2;        /* the same group of buffer A */
	undo_close(a->undo);
	if (core_undo(&e, 0) < 0) return 2;
	printf("lines %zu, mark a on line %zu (2 is right)\n", core_lines(&e), a->mark[0].line + 1);
	int ok = a->mark[0].line == 1;
	unlink(pa);
	unlink(pb);
	core_free(&e);
	return !ok;
}
