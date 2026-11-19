/* The status line against mini.statusline's default content (my-nvim's,
   without the icons and the sections the core does not draw yet) as
   Neovim 0.12.6 lays it out: the texts and faces below are
   nvim_eval_statusline()'s, from tests/test_core_status.lua (measured, 1
   run each).  Then the faces of the windows' other cells, as Neovim's
   highlight groups name them (drawline.c win_line(), win_redr_status()). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "../src/core/core_int.h"

enum { M = FACE_MODE_NORMAL, F = FACE_STATUS_LINE, I = FACE_STATUS_INFO };

/* cells [x, x + n) of row r: their text, and their faces as letters */
static const char *text(const Grid *g, int r, int x, int n)
{
	static char b[1024];
	size_t k = 0;
	for (int c = x; c < x + n; c++) {
		const Cell *z = &g->c[r * g->cols + c];
		memcpy(b + k, grid_text(z), z->len);
		k += z->len;
	}
	b[k] = 0;
	return b;
}

/* the status line of window w: text, then the face of every cell */
static void status(const Grid *g, const Win *w, const char *want, const int *face)
{
	int r = w->y + w->h;
	if (strcmp(text(g, r, w->x, w->w), want)) {
		fprintf(stderr, "width %d:\n  nvim [%s]\n  core [%s]\n", w->w, want, text(g, r, w->x, w->w));
		CHECK(!"status line text");
	}
	/* face: column, face, ..., -1: each face from its column on */
	for (int c = 0, k = 0; c < w->w; c++) {
		while (face[k + 2] >= 0 && c >= face[k + 2]) k += 2;
		if (g->c[r * g->cols + w->x + c].face != face[k + 1]) {
			fprintf(stderr, "width %d [%s]: cell %d has face %d, not %d\n", w->w, want, c,
			        g->c[r * g->cols + w->x + c].face, face[k + 1]);
			CHECK(!"status line face");
		}
	}
}

/* mode, then the file name, a fill, the file info and the location, each
   with mini's padding, faces as nvim_eval_statusline() gave them */
static void line(Editor *e, Grid *g, int cols, const char *m, const char *name, const char *info, const char *loc)
{
	char want[1024];
	int a = (int)strlen(m), lw = grid_width(loc, strlen(loc)), b = cols - lw - (int)strlen(info);
	CHECK(!grid_resize(g, 10, cols));
	core_draw(e, g);
	snprintf(want, sizeof want, "%s%s%*s%s%s", m, name, b - a - (int)strlen(name), "", info, loc);
	int face[] = { 0, M, a, F, b, I, cols - lw, M, -1 };
	status(g, e->win, want, face);
}

int main(void)
{
	static Editor e;
	static Grid g;
	char dir[] = "/tmp/core_statusXXXXXX";
	CHECK(mkdtemp(dir) && !chdir(dir) && !setenv("HOME", dir, 1));
	FILE *f = fopen("f0", "w");
	CHECK(f && fputs("abc\n\tx\n\xe6\xbc\xa2\xe5\xad\x97 y\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, "f0"));

	/* mini.statusline's widths: the mode and the file info shorter below
	   120 columns, %F (home_replace(): "~/f0") from 140, the location's
	   short form below 75 */
	line(&e, &g, 200, " Normal ", " ~/f0 ", " utf-8[unix] 16B ", " 1|3│ 1|3  ");
	line(&e, &g, 140, " Normal ", " ~/f0 ", " utf-8[unix] 16B ", " 1|3│ 1|3  ");
	line(&e, &g, 139, " Normal ", " f0 ", " utf-8[unix] 16B ", " 1|3│ 1|3  ");
	line(&e, &g, 120, " Normal ", " f0 ", " utf-8[unix] 16B ", " 1|3│ 1|3  ");
	line(&e, &g, 119, " N ", " f0 ", "", " 1|3│ 1|3  ");
	line(&e, &g, 80, " N ", " f0 ", "", " 1|3│ 1|3  ");
	line(&e, &g, 75, " N ", " f0 ", "", " 1|3│ 1|3  ");
	line(&e, &g, 74, " N ", " f0 ", "", " 1│ 1 ");
	line(&e, &g, 40, " N ", " f0 ", "", " 1│ 1 ");

	/* too long: %< drops from the file name on, '<' in the face of the
	   last character dropped; what is before %< too long, '>' at the end */
	static const struct { int w; const char *s; int face[9]; } cut[] = {
		{ 12, " N <0  1│ 1 ", { 0, M, 3, F, 6, M, -1 } },
		{ 11, " N <  1│ 1 ", { 0, M, 3, F, 5, M, -1 } },
		{ 8, " N <│ 1 ", { 0, M, -1 } },
		{ 6, " N <1 ", { 0, M, -1 } },
		{ 5, " N < ", { 0, M, -1 } },
		{ 4, " N <", { 0, M, -1 } },
		{ 3, " N>", { 0, M, -1 } },
		{ 2, " >", { 0, M, -1 } },
		{ 1, ">", { 0, M, -1 } },
	};
	CHECK(!grid_resize(&g, 10, 80) && !core_split(&e, 1, 0));
	core_draw(&e, &g);
	for (size_t i = 0; i < sizeof cut / sizeof *cut; i++) {
		core_resize(&e, 1, cut[i].w, 1);
		core_draw(&e, &g);
		CHECK(e.win->w == cut[i].w);
		status(&g, e.win, cut[i].s, cut[i].face);
	}
	/* another window: "%#MiniStatuslineInactive#%F%=", the cell under its
	   separator StatusLineNC's fill, as its status line runs on into the
	   next one (stl_connected()) */
	const Win *o = e.wins == e.win ? e.win->next : e.wins, *l = e.wins;   /* 'splitright' */
	char want[128];
	snprintf(want, sizeof want, "%-*s", o->w, "~/f0");
	status(&g, o, want, (int[]){ 0, FACE_STATUS_LINE_NC, -1 });
	const Cell *z = &g.c[(l->y + l->h) * g.cols + l->x + l->w];
	CHECK(z->ch[0] == ' ' && z->face == FACE_STATUS_LINE_NC);
	CHECK(!core_only(&e));

	/* the location: the cursor's line, the line count, its screen column
	   and its line's width (virtcol("$") - 1) */
	e.win->line = 1;
	e.win->col = 1;
	line(&e, &g, 80, " N ", " f0 ", "", " 2|3│ 9|9  ");
	e.win->line = 2;
	e.win->col = 0;
	line(&e, &g, 80, " N ", " f0 ", "", " 3|3│ 1|6  ");
	CHECK(!core_edit(&e, text_line_start(e.buf->text, 2), 3, "", 0));
	line(&e, &g, 80, " N ", " f0[+] ", "", " 3|3│ 1|4  ");
	line(&e, &g, 130, " Normal ", " f0[+] ", " utf-8[unix] 13B ", " 3|3│ 1|4  ");
	/* the modes: H.modes */
	static const struct { int mode, replace, visual; const char *l, *s; int face; } mode[] = {
		{ MODE_INSERT, 0, 0, " Insert ", " I ", FACE_MODE_INSERT },
		{ MODE_INSERT, 1, 0, " Replace ", " R ", FACE_MODE_REPLACE },
		{ MODE_NORMAL, 0, 'v', " Visual ", " V ", FACE_MODE_VISUAL },
		{ MODE_NORMAL, 0, 'V', " V-Line ", " V-L ", FACE_MODE_VISUAL },
		{ MODE_NORMAL, 0, 22, " V-Block ", " V-B ", FACE_MODE_VISUAL },
		{ MODE_CMDLINE, 0, 0, " Command ", " C ", FACE_MODE_COMMAND },
	};
	for (size_t i = 0; i < sizeof mode / sizeof *mode; i++)
		for (int wide = 0; wide < 2; wide++) {
			e.mode = mode[i].mode;
			e.ks.replace = mode[i].replace;
			e.ks.visual = mode[i].visual;
			e.ks.vline = 2;
			CHECK(!grid_resize(&g, 10, wide ? 130 : 80));
			core_draw(&e, &g);
			const char *m = wide ? mode[i].l : mode[i].s;
			int r = e.win->y + e.win->h, n = (int)strlen(m);
			CHECK(!strcmp(text(&g, r, 0, n), m));
			for (int c = 0; c < n; c++) CHECK(g.c[r * g.cols + c].face == mode[i].face);
			CHECK(g.c[r * g.cols + g.cols - 1].face == mode[i].face);
		}
	e.mode = MODE_NORMAL;
	e.ks.replace = e.ks.visual = 0;

	/* the file info: a file that does not exist has no 'fileencoding';
	   'dos' counts its CRs; KiB and MiB with two decimals */
	CHECK(!core_write(&e, e.buf, 0) && !core_edit_file(&e, "new", 0));
	line(&e, &g, 130, " Normal ", " new ", " [unix] 0B ", " 0|1│ 1|0  ");
	f = fopen("d1", "w");
	CHECK(f && fputs("ab\r\ncd\r\n", f) >= 0 && !fclose(f));
	CHECK(!core_edit_file(&e, "d1", 0));
	line(&e, &g, 130, " Normal ", " d1 ", " utf-8[dos] 8B ", " 1|2│ 1|2  ");
	f = fopen("k1", "w");
	for (int i = 0; i < 200; i++) fputs("123456789\n", f);   /* 2000 bytes */
	CHECK(!fclose(f) && !core_edit_file(&e, "k1", 0));
	line(&e, &g, 130, " Normal ", " k1 ", " utf-8[unix] 1.95KiB ", " 1|200│ 1|9  ");

	/* the window's cells: the cursor line's number in CursorLineNr, the
	   others in LineNr; CursorLine under the cursor line's text to the
	   window's edge, but not in Visual mode; '~' in EndOfBuffer; '@@@' in
	   NonText; the separator in WinSeparator */
	CHECK(!core_edit_file(&e, "f0", 0) && !grid_resize(&g, 10, 40));
	e.win->line = 1;
	e.win->col = 0;
	core_draw(&e, &g);
	int nw = win_numw(e.win);
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < nw - 1; c++)
			CHECK(g.c[r * g.cols + c].face == (r == 1 ? FACE_CURSOR_LINE_NR : FACE_LINE_NR) && !g.c[r * g.cols + c].bg);
		for (int c = nw; c < g.cols; c++) CHECK(g.c[r * g.cols + c].bg == (r == 1 ? FACE_CURSOR_LINE : 0));
	}
	CHECK(g.c[3 * g.cols].ch[0] == '~' && g.c[3 * g.cols].face == FACE_END_OF_BUFFER);
	e.ks.visual = 'V';
	e.ks.vline = 1;
	core_draw(&e, &g);
	for (int c = nw; c < g.cols; c++) CHECK(g.c[g.cols + c].bg != FACE_CURSOR_LINE);
	e.ks.visual = 0;
	CHECK(!core_split(&e, 1, 0));
	core_draw(&e, &g);
	z = &g.c[e.wins->x + e.wins->w];
	CHECK(z->ch[0] == 0xe2 && z->face == FACE_WIN_SEPARATOR);
	/* another window keeps its own cursor line */
	o = e.wins == e.win ? e.win->next : e.wins;
	for (int c = o->x + nw; c < o->x + o->w; c++) CHECK(g.c[g.cols + c].bg == FACE_CURSOR_LINE);
	CHECK(!core_only(&e));
	f = fopen("l1", "w");
	CHECK(f && fputs("a\n", f) >= 0);
	for (int i = 0; i < 400; i++) fputc('x', f);   /* taller than the window */
	CHECK(fputs("\n", f) >= 0 && !fclose(f) && !core_edit_file(&e, "l1", 0));
	core_set_wrap(&e, 1);
	core_draw(&e, &g);
	z = &g.c[(e.win->h - 1) * g.cols + e.win->w - 1];
	CHECK(z->ch[0] == '@' && z->face == FACE_NON_TEXT);
	/* the long line's end in view: its first row from skipcol, "<<<" in
	   NonText after the number, every row of the cursor line on CursorLine */
	e.win->line = 1;
	e.win->col = 399;
	core_set_want(&e);
	core_draw(&e, &g);
	CHECK(e.win->skipcol > 0);
	CHECK(g.c[0].ch[0] == '2' && g.c[0].face == FACE_CURSOR_LINE_NR);   /* "2<<<" */
	for (int c = 1; c < 4; c++) CHECK(g.c[c].ch[0] == '<' && g.c[c].face == FACE_NON_TEXT);
	for (int r = 0; r < e.win->h; r++) CHECK(g.c[r * g.cols + g.cols - 1].bg == FACE_CURSOR_LINE);
	/* the popup's scrollbar: PmenuThumb on PmenuSbar */
	static PumItem items[30];
	for (int i = 0; i < 30; i++) items[i] = (PumItem){ "word", NULL, NULL };
	e.win->line = 0;
	e.win->col = 0;
	core_pum(&e, items, 30, 0, 0);
	core_draw(&e, &g);
	int thumb = 0, sbar = 0;
	for (int r = 0; r < g.rows; r++)
		for (int c = 1; c < g.cols; c++) {
			if (g.c[r * g.cols + c - 1].face != FACE_PMENU && g.c[r * g.cols + c - 1].face != FACE_PMENU_SEL) continue;
			thumb += g.c[r * g.cols + c].face == FACE_PMENU_THUMB;
			sbar += g.c[r * g.cols + c].face == FACE_PMENU_SBAR;
		}
	CHECK(thumb > 0 && sbar > 0);
	core_pum(&e, NULL, 0, -1, 0);
	core_free(&e);
	grid_free(&g);
	printf("test_core_status: ok\n");
	return 0;
}
