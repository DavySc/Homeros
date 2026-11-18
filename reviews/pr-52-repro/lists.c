/* Review of PR 52: the change list, the jump list and marks a b c under the
   delete lists.lua makes in Neovim (d5w from 1,4 through the end of line 3
   of "abc def\nxyz uvw\nfoo bar\nbar baz\nqq\n"), made as package 5's keys
   make it: core_mark_join() for the end line, then one core_edit().
   Prints what lists.lua prints (0-based columns, 1-based lines).
   Build: make -f Makefile -f reviews/pr-52-repro/drv.mk DRV=reviews/pr-52-repro/lists.c build/san/lists */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "core.h"
#include "undo.h"

static void show(Editor *e, const char *tag)
{
	Buf *b = e->buf;
	printf("%s changes:", tag);
	for (int k = 0; k < b->nchange; k++) printf(" (%zu,%zu)", b->change[k].line + 1, b->change[k].col);
	printf(" jumps:");
	for (int k = 0; k < e->win->njump; k++) printf(" (%zu,%zu)", e->win->jump[k].m.line + 1, e->win->jump[k].m.col);
	const char *n[] = { "'a", "'b", "'c", "'<" };
	Mark m[] = { b->mark[0], b->mark[1], b->mark[2], b->mark[MARK_LT] };
	for (int k = 0; k < 4; k++)
		if (m[k].set) printf(" %s=(%zu,%zu)", n[k], m[k].line + 1, m[k].col);
		else printf(" %s=(0,-1)", n[k]);
	printf("\n");
}

int main(void)
{
	static Editor e;
	char path[] = "/tmp/pr52listsXXXXXX";
	FILE *f = fdopen(mkstemp(path), "w");
	fputs("abc def\nxyz uvw\nfoo bar\nbar baz\nqq\n", f);
	fclose(f);
	if (core_open(&e, path)) return 2;
	unlink(path);
	Buf *b = e.buf;
	Mark p[] = { { 1, 4, 1 }, { 2, 2, 1 }, { 3, 1, 1 } };
	for (int k = 0; k < 3; k++) {
		b->mark[k] = b->change[k] = p[k];
		e.win->jump[k].buf = b;
		e.win->jump[k].m = p[k];
	}
	b->nchange = e.win->njump = e.win->jumpidx = 3;
	b->mark[MARK_LT] = b->mark[MARK_GT] = p[1];
	e.win->col = 4;
	show(&e, "before");
	undo_close(b->undo);
	UndoCursor c = { 0, 4, b->flags };
	undo_record(b->undo, 0, "", 0, "", 0, c, b->flags);      /* u_save_cursor() */
	core_mark_join(&e, 2, 0, -2, 4, 0);
	core_edit(&e, 4, core_line_start(&e, 2) + core_line_len(&e, 2) - 4, "", 0);
	undo_close(b->undo);
	show(&e, "after ");
	core_undo(&e, 0);
	show(&e, "undone");
	core_undo(&e, 1);
	show(&e, "redone");
	core_free(&e);
	return 0;
}
