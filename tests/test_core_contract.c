/* The core.h clauses the screen traces cannot see (review of PR 20, 9):
   the return values that end a macro, the quit check, the command line's
   row and cursor, the selected popup item.  The beeps are Neovim's,
   measured with a macro "<key>Ax<Esc>" run by feedkeys on a 100-line
   buffer, 24 x 80, the cursor line centred first (1 run each): CTRL-D on
   the last line, CTRL-U and CTRL-B on the first, gj on the last and gk on
   the first beep; from line 50 none does. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"

static char path[] = "/tmp/core_contractXXXXXX";

static void at(Editor *e, size_t l)
{
	e->win->line = l;
	e->win->col = 0;
	core_set_want(e);
	core_scroll_cursor(e, 'z');
	core_update(e);
}

int main(void)
{
	static Editor e;
	static Grid g;
	CHECK(mkstemp(path) >= 0);
	FILE *f = fopen(path, "w");
	CHECK(f);
	for (int i = 1; i <= 100; i++) fprintf(f, "%d\n", i);
	CHECK(!fclose(f));
	CHECK(!core_open(&e, path) && !grid_resize(&g, 24, 80));
	core_update(&e);

	/* scrolling and gj/gk: 0 moved, -1 a beep */
	struct { size_t from; int r[6]; } want[] = {   /* CTRL-D, CTRL-U, CTRL-F, CTRL-B, gj, gk */
		{ 0, { 0, -1, 0, -1, 0, -1 } }, { 49, { 0, 0, 0, 0, 0, 0 } }, { 99, { -1, 0, 0, 0, -1, 0 } },
	};
	for (int i = 0; i < 3; i++) {
		at(&e, want[i].from); CHECK(core_scroll_half(&e, 1, 0) == want[i].r[0]);
		at(&e, want[i].from); CHECK(core_scroll_half(&e, -1, 0) == want[i].r[1]);
		at(&e, want[i].from); CHECK(core_scroll_page(&e, 1, 1) == want[i].r[2]);
		at(&e, want[i].from); CHECK(core_scroll_page(&e, -1, 1) == want[i].r[3]);
		at(&e, want[i].from); CHECK(core_screen_move(&e, 1) == want[i].r[4]);
		at(&e, want[i].from); CHECK(core_screen_move(&e, -1) == want[i].r[5]);
		core_set_wrap(&e, 1);                      /* gj and gk by screen line */
		at(&e, want[i].from); CHECK(core_screen_move(&e, 1) == want[i].r[4]);
		at(&e, want[i].from); CHECK(core_screen_move(&e, -1) == want[i].r[5]);
		core_set_wrap(&e, 0);
	}

	/* the command line: its type at column 0 of the row under the
	   windows, the text after it, the cursor on byte cmdpos */
	e.mode = MODE_CMDLINE;
	e.cmdtype = ':';
	strcpy(e.cmd, "s\xce\xbbx");
	e.cmdlen = 4;
	e.cmdpos = 3;
	core_update(&e);
	core_draw(&e, &g);
	int r = e.rows - 1;                        /* cmdheight 1 */
	const Cell *c = &g.c[r * g.cols];
	CHECK(c[0].len == 1 && c[0].ch[0] == ':' && c[1].ch[0] == 's' && c[2].len == 2 && c[3].ch[0] == 'x');
	CHECK(g.crow == r && g.ccol == 3);
	e.mode = MODE_NORMAL;

	/* the popup: the selected item drawn plainly, the others reversed
	   (PmenuSel against Pmenu) */
	static const PumItem items[] = { { "foo", NULL, NULL }, { "foobar", NULL, NULL }, { "fop", NULL, NULL } };
	at(&e, 10);
	core_pum(&e, items, 3, 1, 0);
	core_draw(&e, &g);
	int seen = 0;
	for (int y = 0; y < g.rows; y++)
		for (int x = 0; x + 2 < g.cols; x++) {
			const Cell *p = &g.c[y * g.cols + x];
			if (p[0].ch[0] != 'f' || p[1].ch[0] != 'o') continue;
			if (p[2].ch[0] == 'o' && p[3].ch[0] == 'b') { CHECK(p->attr == ATTR_NONE); seen |= 1; }
			else if (p[2].ch[0] == 'o' || p[2].ch[0] == 'p') { CHECK(p->attr == ATTR_REV); seen |= 2; }
		}
	CHECK(seen == 3);
	core_pum(&e, NULL, 0, -1, 0);

	/* the last window closes only without unsaved changes (E37), or with ! */
	CHECK(!core_edit(&e, 0, 0, "z", 1));
	CHECK(core_close(&e, 0) == -1 && !e.quit && strstr(e.msg, "E37"));
	CHECK(!core_close(&e, 1) && e.quit);

	core_free(&e);

	/* buffers: a split keeps the alternate (win_init()); :bd of the last
	   one edited goes to the newest other buffer in the jump list
	   (measured in nvim, 1 run: f0, :e f1, :e f2, :e f3, :sp has f2 as
	   alternate; :close, :bd shows f2) */
	char fn[4][96];
	for (int i = 0; i < 4; i++) {
		snprintf(fn[i], sizeof fn[i], "%s.%d", path, i);
		FILE *h = fopen(fn[i], "w");
		CHECK(h && fputs("x\n", h) >= 0 && !fclose(h));
	}
	CHECK(!core_open(&e, fn[0]));
	for (int i = 1; i < 4; i++) CHECK(!core_edit_file(&e, fn[i], 0));
	Buf *alt = e.win->alt;
	CHECK(alt && !strcmp(alt->path, fn[2]) && !core_split(&e, 0, 0) && e.win->alt == alt);
	CHECK(!core_close(&e, 0));
	CHECK(!core_buf_delete(&e, 0, 0) && !strcmp(e.buf->path, fn[2]));
	core_free(&e);
	for (int i = 0; i < 4; i++) unlink(fn[i]);
	grid_free(&g);
	unlink(path);
	printf("test_core_contract: ok\n");
	return 0;
}
