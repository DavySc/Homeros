/* The status line against lualine with catppuccin's lualine theme, set up
   as UI proposal 5's mockup (tests/test_core_status.lua), as Neovim 0.12.6
   lays it out: tests/test_core_status.txt holds nvim_eval_statusline()'s
   text and highlight groups for each state below (measured, 1 run each;
   lualine 221ce6b, catppuccin 05e8787).  Then the faces of the windows'
   other cells, as Neovim's highlight groups name them (drawline.c
   win_line(), win_redr_status()). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "uni.h"
#include "../src/core/core_int.h"

static char rec[64][1024];
static int nrec;

/* lualine's highlight group as the face of a cell and its background */
static void group(const char *s, size_t n, unsigned char *face, unsigned char *bg)
{
	static const char *const mode[] = { "normal", "insert", "visual", "replace", "command" };
	static const unsigned char a[] = { FACE_MODE_NORMAL, FACE_MODE_INSERT, FACE_MODE_VISUAL, FACE_MODE_REPLACE, FACE_MODE_COMMAND },
	                           b[] = { FACE_MODE_NORMAL_FG, FACE_MODE_INSERT_FG, FACE_MODE_VISUAL_FG, FACE_MODE_REPLACE_FG,
	                                   FACE_MODE_COMMAND_FG };
	char g[128];
	CHECK(n < sizeof g);
	memcpy(g, s, n);
	g[n] = 0;
	const char *t = strncmp(g, "lualine_transitional_", 21) ? g : g + 21;
	int m = 0;
	while (m < 4 && strncmp(t + 10, mode[m], strlen(mode[m]))) m++;
	*bg = 0;
	if (!strcmp(g, "lualine_c_normal")) *face = FACE_STATUS_LINE;
	else if (!strcmp(g, "lualine_c_inactive")) *face = FACE_STATUS_LINE_NC;
	else if (t == g) {
		CHECK(!strncmp(g, "lualine_a_", 10) || !strncmp(g, "lualine_b_", 10));
		*face = g[8] == 'a' ? a[m] : b[m];
		*bg = g[8] == 'a' ? 0 : FACE_STATUS_INFO;
	} else if (t[8] == 'a') {
		/* a pill's end: fg the pill's bg, bg the section's beside it */
		*face = b[m];
		*bg = strstr(t, "_to_lualine_b_") ? FACE_STATUS_INFO : 0;
	} else {
		CHECK(!strncmp(t, "lualine_b_", 10) && strstr(t, "_to_lualine_c_normal"));
		*face = FACE_STATUS_INFO;
		*bg = FACE_STATUS_LINE;
	}
}

/* the status line of window w against the recorded state tag: each cell's
   text, face and background */
static void status(const Grid *g, const Win *w, const char *tag)
{
	const char *line = NULL;
	for (int i = 0; i < nrec && !line; i++)
		if (!strncmp(rec[i], tag, strlen(tag)) && rec[i][strlen(tag)] == '\t') line = rec[i] + strlen(tag) + 1;
	CHECK(line);
	const char *hl = strchr(line, '\t');
	const unsigned char *s = (const unsigned char *)line;
	size_t n = (size_t)(hl - line);
	int r = w->y + w->h, c = 0;
	for (size_t i = 0; i < n; i += uni_glen(s, n, i)) {
		/* the group of byte i: the last that starts at or before it */
		unsigned char face = FACE_STATUS_LINE, bg = 0;
		for (const char *h = hl + 1; *h && *h != '\n';) {
			char *colon;
			size_t at = strtoul(h, &colon, 10), k = strcspn(colon + 1, " \n");
			if (at <= i) group(colon + 1, k, &face, &bg);
			h = colon + 1 + k + (colon[1 + k] == ' ');
		}
		const Cell *z = &g->c[r * g->cols + w->x + c];
		size_t k = uni_glen(s, n, i);
		if (z->len != k || memcmp(grid_text(z), s + i, k) || z->face != face || z->bg != bg) {
			fprintf(stderr, "%s, cell %d: nvim [%.*s] face %d bg %d, core [%.*s] face %d bg %d\n  nvim [%.*s]\n", tag, c,
			        (int)k, s + i, face, bg, z->len, grid_text(z), z->face, z->bg, (int)n, line);
			CHECK(!"status line");
		}
		c += uni_width(s, n, i);
	}
	CHECK(c == w->w);
}

static void draw(Editor *e, Grid *g, int cols, const char *tag)
{
	CHECK(!grid_resize(g, 10, cols));
	core_draw(e, g);
	status(g, e->win, tag);
}

static void file(const char *name, const char *s)
{
	FILE *f = fopen(name, "w");
	CHECK(f && fputs(s, f) >= 0 && !fclose(f));
}

int main(void)
{
	static Editor e;
	static Grid g;
	FILE *f = fopen("tests/test_core_status.txt", "r");
	CHECK(f);
	while (nrec < 64 && fgets(rec[nrec], sizeof *rec, f)) nrec++;
	CHECK(!fclose(f));
	char dir[] = "/tmp/core_statusXXXXXX";
	CHECK(mkdtemp(dir) && !chdir(dir) && !setenv("HOME", dir, 1));
	file("f0", "abc\n\tx\n\xe6\xbc\xa2\xe5\xad\x97 y\n");
	CHECK(!core_open(&e, "f0"));
	draw(&e, &g, 120, "width 120");
	draw(&e, &g, 80, "width 80");
	draw(&e, &g, 40, "width 40");

	/* too long: %< (after the mode) drops from the file name on, '<' in
	   the face of the last character dropped; what is before %< too long,
	   '>' at the end */
	static const int cut[] = { 30, 20, 16, 12, 10, 8, 6, 4, 3, 2, 1 };
	CHECK(!grid_resize(&g, 10, 80) && !core_split(&e, 1, 0));
	core_draw(&e, &g);
	char tag[32];
	for (size_t i = 0; i < sizeof cut / sizeof *cut; i++) {
		core_resize(&e, 1, cut[i], 1);
		core_draw(&e, &g);
		CHECK(e.win->w == cut[i]);
		snprintf(tag, sizeof tag, "width %d", cut[i]);
		status(&g, e.win, tag);
	}
	/* another window: lualine's inactive sections, the cell under its
	   separator StatusLineNC's fill, as its status line runs on into the
	   next one (stl_connected()) */
	const Win *o = e.wins == e.win ? e.win->next : e.wins, *l = e.wins;   /* 'splitright' */
	status(&g, o, "other");
	const Cell *z = &g.c[(l->y + l->h) * g.cols + l->x + l->w];
	CHECK(z->ch[0] == ' ' && z->face == FACE_STATUS_LINE_NC);
	CHECK(!core_only(&e));

	/* the location: line('.') and charcol('.') (a Tab is one character);
	   the progress */
	e.win->line = 1;
	e.win->col = 1;
	draw(&e, &g, 80, "2,1");
	e.win->line = 2;
	e.win->col = 7;
	draw(&e, &g, 80, "3,7");
	CHECK(!core_edit(&e, text_line_start(e.buf->text, 2), 3, "", 0));
	e.win->col = 4;
	draw(&e, &g, 80, "x");
	/* the modes: lualine's Mode.map, the pills in the mode's colours */
	static const struct { int mode, replace, visual; const char *tag; } mode[] = {
		{ MODE_INSERT, 0, 0, "mode i" }, { MODE_INSERT, 1, 0, "mode R" }, { MODE_NORMAL, 0, 'v', "mode v" },
		{ MODE_NORMAL, 0, 'V', "mode V" }, { MODE_NORMAL, 0, 22, "mode CTRL-V" }, { MODE_CMDLINE, 0, 0, "mode c" },
	};
	for (size_t i = 0; i < sizeof mode / sizeof *mode; i++) {
		e.mode = mode[i].mode;
		e.ks.replace = mode[i].replace;
		e.ks.visual = mode[i].visual;
		e.ks.vline = 2;
		draw(&e, &g, 80, mode[i].tag);
	}
	e.mode = MODE_NORMAL;
	e.ks.replace = e.ks.visual = 0;

	/* a file that does not exist has no 'fileencoding', nor has a buffer
	   without one; an empty file that exists has */
	CHECK(!core_write(&e, e.buf, 0) && !core_edit_file(&e, "new", 0));
	draw(&e, &g, 80, "new");
	CHECK(!core_edit_file(&e, "", 0));
	draw(&e, &g, 80, "noname");
	f = fopen("k1", "w");
	for (int i = 0; i < 200; i++) fputs("123456789\n", f);
	CHECK(!fclose(f) && !core_edit_file(&e, "k1", 0));
	static const struct { size_t line; const char *tag; } k1[] = { { 99, "k1 100" }, { 1, "k1 2" }, { 57, "k1 58" } };
	for (size_t i = 0; i < sizeof k1 / sizeof *k1; i++) {
		e.win->line = k1[i].line;
		draw(&e, &g, 80, k1[i].tag);
	}
	/* a combining mark is one character with its base */
	file("c1", "e\xcc\x81" "e\xcc\x81x\n");
	CHECK(!core_edit_file(&e, "c1", 0));
	e.win->col = 6;
	draw(&e, &g, 80, "c1");
	file("e0", "");
	CHECK(!core_edit_file(&e, "e0", 0));
	draw(&e, &g, 80, "e0");

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
