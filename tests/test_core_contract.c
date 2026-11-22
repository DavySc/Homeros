/* The core.h clauses the screen traces cannot see (review of PR 20, 9):
   the return values that end a macro, the quit check, the command line's
   row and cursor, the selected popup item.  The beeps are Neovim's,
   measured with a macro "<key>Ax<Esc>" run by feedkeys on a 100-line
   buffer, 24 x 80, the cursor line centred first (1 run each): CTRL-D on
   the last line, CTRL-U and CTRL-B on the first, gj on the last and gk on
   the first beep; from line 50 none does. */
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "lsp.h"
#include "syntax.h"
#include "grid.h"
#include "../src/core/core_int.h"

static char path[] = "/tmp/core_contractXXXXXX";

static void at(Editor *e, size_t l)
{
	core_goto(e, l, 0);
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
	/* core_goto past the end: the last line's last character, wanted there */
	core_goto(&e, 1000, 1000);
	CHECK(e.win->line == 99 && e.win->col == 2 && e.win->want == 2);

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

	/* H and L: scrolloff only when asked (nv_scroll() skips cursor_correct()
	   under an operator) */
	at(&e, 49);
	CHECK(core_screen_line(&e, 'H', 1, 0) == e.win->top && core_screen_line(&e, 'H', 1, 1) == e.win->top + 6);

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
	/* showmode() in Replace mode (measured in tmux, 1 run: R shows it) */
	e.mode = MODE_INSERT;
	e.ks.replace = 1;
	core_draw(&e, &g);
	c = &g.c[(g.rows - 1) * g.cols];
	CHECK(c[0].ch[0] == '-' && c[3].ch[0] == 'R' && c[9].ch[0] == 'E' && c[11].ch[0] == '-');
	e.ks.replace = 0;
	e.mode = MODE_NORMAL;

	/* the popup: the selected item in PmenuSel, the others in Pmenu */
	static const PumItem items[] = { { "foo", NULL, NULL }, { "foobar", NULL, NULL }, { "fop", NULL, NULL } };
	at(&e, 10);
	core_pum(&e, items, 3, 1, 0);
	core_draw(&e, &g);
	int seen = 0;
	for (int y = 0; y < g.rows; y++)
		for (int x = 0; x + 2 < g.cols; x++) {
			const Cell *p = &g.c[y * g.cols + x];
			if (p[0].ch[0] != 'f' || p[1].ch[0] != 'o') continue;
			if (p[2].ch[0] == 'o' && p[3].ch[0] == 'b') { CHECK(p->face == FACE_PMENU_SEL); seen |= 1; }
			else if (p[2].ch[0] == 'o' || p[2].ch[0] == 'p') { CHECK(p->face == FACE_PMENU); seen |= 2; }
		}
	CHECK(seen == 3);
	core_pum(&e, NULL, 0, -1, 0);

	/* a byte inside a cluster has the cluster's start column (getvcol();
	   package 5 measured nvim on "x" U+0301: the composing byte is at 0) */
	CHECK(!core_edit(&e, 0, 0, "x\xcc\x81\t", 4));
	CHECK(core_vcol(&e, 0, 1) == 0 && core_vcol(&e, 0, 2) == 0 && core_vcol(&e, 0, 3) == 1 && core_vcol(&e, 0, 4) == 8);

	/* Visual mode's Tab rule is the current window's: another window on the
	   buffer keeps its cursor on a Tab's last cell (its w_virtcol stays;
	   measured: its old ruler "1,1-8" after v and :redraw!, 3 runs) */
	CHECK(!core_split(&e, 0, 0));
	e.ks.visual = 'v';
	e.ks.vline = e.win->line = 0;
	e.ks.vcol = e.win->col = 3;             /* the Tab after "x\xcc\x81" */
	Win *o = e.wins == e.win ? e.win->next : e.wins;
	o->line = 0;
	o->col = 3;
	CHECK(win_vcol(&e, e.win, NULL, NULL) == 1 && win_vcol(&e, o, NULL, NULL) == 7);
	e.ks.visual = 0;
	CHECK(!core_close(&e, 0));

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
	/* buflist_findname(): a file's name as typed and its full name are one
	   buffer */
	Buf *same = e.buf;
	CHECK(!chdir("/tmp") && !core_edit_file(&e, strrchr(fn[3], '/') + 1, 0) && e.buf == same);
	Buf *alt = e.win->alt;
	CHECK(alt && !strcmp(alt->path, fn[2]) && !core_split(&e, 0, 0) && e.win->alt == alt);
	CHECK(!core_close(&e, 0));
	CHECK(!core_buf_delete(&e, 0, 0) && !strcmp(e.buf->path, fn[2]));
	core_free(&e);
	/* :bd closes the buffer's language server document once: core_free
	   after it does not close it again (ASan; review of PR 111) */
	{
		static const char *const cat[] = { "sh", "-c", "cat >/dev/null", NULL }, *const none[] = { NULL };
		const LspServer srv[] = { { ".lspt", "lspt", cat, none, 0 }, { 0 } }, *was = lsp_servers;
		char f[128];
		snprintf(f, sizeof f, "%s.lspt", path);
		FILE *h = fopen(f, "w");
		CHECK(h && fputs("x\n", h) >= 0 && !fclose(h));
		lsp_servers = srv;
		CHECK(!core_open(&e, fn[0]) && !core_edit_file(&e, f, 0) && e.buf->lsp);
		CHECK(!core_buf_delete(&e, 0, 0) && !strcmp(e.buf->path, fn[0]));
		core_free(&e);
		lsp_servers = was;
		unlink(f);
	}
	/* an outside change not yet warned is forgotten by a save over it:
	   the next edit gives no W11 */
	CHECK(!core_open(&e, fn[0]));
	{
		char other[128];
		snprintf(other, sizeof other, "%s.new", path);
		FILE *h = fopen(other, "w");
		CHECK(h && fputs("y\n", h) >= 0 && !fclose(h) && !rename(other, fn[0]));
		struct pollfd pf = { e.buf->watch, POLLIN, 0 };
		CHECK(e.buf->watch >= 0 && poll(&pf, 1, 1000) == 1);
	}
	core_watch(&e, e.buf->watch);
	CHECK(!core_write(&e, e.buf, 1));
	CHECK(!core_edit(&e, 0, 0, "z", 1) && !strstr(e.msg, "W11"));
	CHECK(!core_close(&e, 1));
	core_free(&e);
	/* CTRL-W > in a terminal shrunk below what the windows need: the
	   window grows as far as its row of windows lets it, as Neovim's
	   frame_setwidth() stops at a frame as tall as ROWS_AVAIL (measured
	   in tmux: 12x40, :3vs :3sp :10vs :new, terminal 4x15, 17 CTRL-W >;
	   review of PR 62, 1) */
	CHECK(!core_open(&e, fn[0]) && !grid_resize(&g, 12, 40));
	core_draw(&e, &g);
	CHECK(!core_split(&e, 1, 3) && !core_split(&e, 0, 3) && !core_split(&e, 1, 10) && !core_new(&e, 0));
	CHECK(!grid_resize(&g, 4, 15));
	core_draw(&e, &g);
	core_resize(&e, 1, 17, 0);
	{
		int k = 0, wd[] = { 1, 13, 1, 11, 11 };
		for (Win *w = e.wins; w; w = w->next, k++) CHECK(k < 5 && w->w == wd[k]);
		CHECK(k == 5);
	}
	core_free(&e);
	/* the view is drawn after each command, before the next one runs
	   (normal_check()'s normal_redraw(), typeahead or not): wrap, CTRL-W s,
	   6G$, CTRL-W w puts the lower window's top at line 3 (measured in tmux,
	   20x50, line 6 of 60 or 140 characters: tops 3 and 4; render-markdown's
	   case 738) */
	{
		FILE *h = fopen(fn[1], "w");
		CHECK(h && fprintf(h, "a\na\na\na\na\n%060d\nb\nb\nb\nb\nb\nb\nb\nb\n", 0) > 0 && !fclose(h));
	}
	CHECK(!core_open(&e, fn[1]) && !grid_resize(&g, 20, 50));
	core_set_wrap(&e, 1);
	core_draw(&e, &g);
	CHECK(!core_split(&e, 0, 0));
	core_update(&e);
	e.win->line = 5;
	e.win->col = 59;
	core_set_want(&e);
	core_update(&e);
	CHECK(!core_win_goto(&e, 'w', 0));
	core_update(&e);
	core_draw(&e, &g);
	CHECK(e.wins->next && e.wins->next->top == 2);
	/* the Visual area shows in every window on the buffer (win_line():
	   wp->w_buffer == curwin->w_buffer) */
	e.ks.visual = 'V';
	e.ks.vline = 5;
	core_draw(&e, &g);
	for (const Win *w = e.wins; w; w = w->next)
		CHECK(w->top <= 5 && g.c[(w->y + 5 - (int)w->top) * g.cols + w->x + win_numw(w)].bg == FACE_VISUAL);
	/* an area that starts on a line's NUL shows one cell there (win_line()'s
	   "invert at least one char"): v$ojj on "abc" shows the cell after c
	   (measured in nvim --headless, 3 runs) */
	e.ks.visual = 'v';
	e.ks.vline = 2;
	e.ks.vcol = 1;
	e.win->line = 4;
	e.win->col = 0;
	core_update(&e);
	core_draw(&e, &g);
	CHECK(e.win->top <= 2 && g.c[(e.win->y + 2 - (int)e.win->top) * g.cols + e.win->x + win_numw(e.win) + 1].bg == FACE_VISUAL);
	e.ks.visual = 0;

	/* a heading's background stops at the grid's edge when the windows
	   reach past a terminal too small for them (fuzz_core's case, which
	   the fuzzer once missed in 120 s) */
	char md[] = "/tmp/core_contractXXXXXX.md";
	CHECK(mkstemps(md, 3) >= 0 && (f = fopen(md, "w")) && fputs("# heading\nx\n", f) >= 0 && !fclose(f));
	CHECK(!core_edit_file(&e, md, 1) && !grid_resize(&g, 24, 80));
	e.win->line = 1;
	do core_draw(&e, &g);
	while (syntax_update(e.buf->syn, e.buf->text, NULL));
	CHECK(g.c[win_numw(e.win)].bg == FACE_HEADING_1_BG);   /* not on the cursor line */
	/* nine windows one above another need 18 rows and the command line;
	   the grid has 6 */
	for (int i = 0; i < 8; i++) CHECK(!core_split(&e, 0, 0));
	for (Win *w = e.wins; w; w = w->next)
		if (w != e.win) w->line = w->top = 0;   /* the heading in view, not under the cursor */
	CHECK(!grid_resize(&g, 6, 80));
	core_draw(&e, &g);
	CHECK(e.layout->size[ROWS] > g.rows);
	CHECK(!core_only(&e));
	/* core_buf_fill: both windows on the buffer clamped to the new text, or
	   kept where they were (SIZE_MAX); found by number while listed */
	CHECK(!core_edit_file(&e, "", 1) && !core_split(&e, 0, 0));
	Buf *nb = e.buf;
	CHECK(core_buf_nr(&e, nb->nr) == nb && !core_buf_nr(&e, 9999));
	core_buf_fill(&e, nb, "a\nb\nc\nd\n", 8, 9);
	for (Win *w = e.wins; w; w = w->next) { CHECK(w->line == 3); w->top = 3; w->col = 1; }
	e.wins->line = 1;
	core_buf_fill(&e, nb, "p\nq\nr\n", 6, SIZE_MAX);
	CHECK(e.wins->line == 1 && e.wins->next->line == 2 && e.win->col == 0 && e.wins->top == 1 && e.wins->next->top == 2);
	core_buf_fill(&e, nb, "z\n", 2, 1);
	for (Win *w = e.wins; w; w = w->next) CHECK(w->line == 0 && w->top == 0 && !(nb->flags & CORE_EMPTY));
	CHECK(!core_only(&e));
	int nr = nb->nr;
	CHECK(!core_buf_delete(&e, nr, 1) && !core_buf_nr(&e, nr));   /* :bd keeps it, unlisted */
	unlink(md);
	/* on_yank's flash shows only in the window it was yanked in (the
	   namespace is scoped to it: nvim__ns_set()) */
	e.ks.yank.type = 'V';
	e.ks.yank.l1 = e.ks.yank.l2 = e.win->line;
	CHECK(!core_split(&e, 1, 0));
	core_draw(&e, &g);
	for (const Win *w = e.wins; w; w = w->next) {
		const Cell *z = &g.c[(w->y + (int)(w->line - w->top)) * g.cols + w->x + win_numw(w)];
		CHECK((z->bg == FACE_INC_SEARCH) == (w == e.win));
	}
	CHECK(!core_only(&e));
	/* and only on text: none on an empty line, where Visual shows a cell
	   ("a", "", "bc", 2Gvjy: measured in nvim --headless, 3 runs) */
	char ey[] = "/tmp/core_contractXXXXXX";
	CHECK(mkstemp(ey) >= 0 && (f = fopen(ey, "w")) && fputs("a\n\nbc\n", f) >= 0 && !fclose(f));
	CHECK(!core_edit_file(&e, ey, 1));
	e.ks.yank.type = 'v';
	e.ks.yank.l1 = 1;
	e.ks.yank.l2 = 2;
	e.ks.yank.c1 = e.ks.yank.c2 = 0;
	e.win->line = 1;
	e.win->col = 0;
	core_draw(&e, &g);
	CHECK(e.win->top == 0 && g.c[g.cols + win_numw(e.win)].bg == FACE_CURSOR_LINE);
	CHECK(g.c[2 * g.cols + win_numw(e.win)].bg == FACE_INC_SEARCH);
	e.ks.yank.type = 0;
	/* :s's c flag shows its match the same way: "b" of "bc", not "c" */
	e.ks.inc = (KeyArea){ 'v', 2, 0, 2, 0 };
	e.win->line = 0;
	core_draw(&e, &g);
	CHECK(g.c[2 * g.cols + win_numw(e.win)].bg == FACE_INC_SEARCH && g.c[2 * g.cols + win_numw(e.win) + 1].bg != FACE_INC_SEARCH);
	e.ks.inc.type = 0;
	/* the flash as the keys set it: vly over "é漢x" covers é and both
	   cells of 漢, not x (vim.hl.on_yank's '[ to ']) */
	CHECK((f = fopen(ey, "w")) && fputs("\xc3\xa9\xe6\xbc\xa2x\n", f) >= 0 && !fclose(f));
	CHECK(!core_edit_file(&e, ey, 1));
	keys_feed(&e, 'v', 0);
	keys_feed(&e, 'l', 0);
	keys_feed(&e, 'y', 0);
	core_draw(&e, &g);
	/* cell 2 is 漢's second half, drawn with its first */
	for (int c = 0; c < 4; c++) if (c != 2) CHECK((g.c[win_numw(e.win) + c].bg == FACE_INC_SEARCH) == (c < 3));
	e.ks.yank.type = 0;
	/* 'hlsearch' over the flash (an extmark under win_line()'s
	   search_attr): "ab ab ab ab", /ab, gg0ly3l shows C C I S S
	   (measured, reviews/pr-106-repro/flash_search.lua) */
	CHECK((f = fopen(ey, "w")) && fputs("ab ab ab ab\n", f) >= 0 && !fclose(f));
	CHECK(!core_edit_file(&e, ey, 1));
	for (const char *k = "/ab\rgg0ly3l"; *k; k++) keys_feed(&e, *k == '\r' ? KEY_ENTER : *k, 0);
	core_draw(&e, &g);
	const unsigned char fs[] = { FACE_CUR_SEARCH, FACE_CUR_SEARCH, FACE_INC_SEARCH, FACE_SEARCH, FACE_SEARCH };
	for (int c = 0; c < 5; c++) CHECK(g.c[win_numw(e.win) + c].bg == fs[c]);
	e.ks.yank.type = 0;
	unlink(ey);
	core_free(&e);
	for (int i = 0; i < 4; i++) unlink(fn[i]);
	grid_free(&g);
	unlink(path);
	printf("test_core_contract: ok\n");
	return 0;
}
