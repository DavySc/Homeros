/* Undo and redo in the core where the bytes alone do not say what Neovim
   saved: an empty line joined to an empty line removes the same "\n" as dd
   on it, but u_save() kept the joined line, so redo leaves the cursor on the
   line it started on (measured in conform, seed 1794930960); the marks a
   group saved come back (u_undoredo()). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"

static char path[] = "/tmp/core_undoXXXXXX";

static void load(Editor *e, const char *text)
{
	FILE *f = fopen(path, "w");
	CHECK(f && fputs(text, f) >= 0 && !fclose(f));
	CHECK(!core_open(e, path));
}

/* the "\n" before line l removed (Backspace at its start), as one group,
   the cursor at line `from` first; join: the keys report it */
static void remove_nl(Editor *e, size_t l, int join)
{
	undo_close(e->buf->undo);
	e->win->line = l;
	e->win->col = 0;
	size_t pl = core_line_len(e, l - 1);
	if (join) core_mark_join(e, l, 0, -1, (long)pl, 0);   /* before, as do_join() */
	CHECK(!core_edit(e, core_line_start(e, l) - 1, 1, "", 0));
	e->win->line = l - 1;
	e->win->col = pl;
	undo_close(e->buf->undo);
}

int main(void)
{
	static Editor e;
	CHECK(mkstemp(path) >= 0);
	/* the join: redo goes back to the line the group started on */
	load(&e, "x\n\n\ny\n");
	remove_nl(&e, 2, 1);
	CHECK(core_lines(&e) == 3);
	CHECK(core_undo(&e, 0) >= 0 && core_lines(&e) == 4);
	CHECK(core_undo(&e, 1) >= 0 && core_lines(&e) == 3 && e.win->line == 2);
	core_free(&e);
	/* the same bytes as dd on the empty line: whole lines, the first one */
	load(&e, "x\n\n\ny\n");
	remove_nl(&e, 2, 0);
	CHECK(core_undo(&e, 0) >= 0 && core_undo(&e, 1) >= 0 && e.win->line == 1);
	core_free(&e);
	/* a join moves the marks of the joined line onto the line before */
	load(&e, "ab\ncd\nef\n");
	e.buf->mark[0] = (Mark){ 1, 1, 1 };
	e.buf->mark[1] = (Mark){ 2, 1, 1 };
	remove_nl(&e, 1, 1);
	CHECK(e.buf->mark[0].set && e.buf->mark[0].line == 0 && e.buf->mark[0].col == 3);
	CHECK(e.buf->mark[1].set && e.buf->mark[1].line == 1 && e.buf->mark[1].col == 1);
	core_free(&e);
	/* a mark set after a group opened is back where it was on undo, and
	   the one undo replaced returns on redo (u_undoredo()'s swap) */
	load(&e, "a\nb\nc\n");
	e.buf->mark[0] = (Mark){ 2, 0, 1 };
	undo_close(e.buf->undo);
	CHECK(!core_edit(&e, 0, 0, "z", 1));      /* "za": no mark moves */
	undo_close(e.buf->undo);
	e.buf->mark[0] = (Mark){ 0, 1, 1 };       /* set again after the change */
	CHECK(core_undo(&e, 0) >= 0 && e.buf->mark[0].line == 2 && e.buf->mark[0].col == 0);
	CHECK(core_undo(&e, 1) >= 0 && e.buf->mark[0].line == 0 && e.buf->mark[0].col == 1);
	core_free(&e);
	unlink(path);
	printf("test_core_undo: ok\n");
	return 0;
}
