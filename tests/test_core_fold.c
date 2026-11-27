/* Closed folds against Neovim (package 4): replays the cases that
   tests/test_core_fold.py recorded (a text, a fold level per line, a
   window, commands) and compares after each command the window's rows,
   which cells are Folded, the top line and the cursor.  The folds are
   fold.h's, from the recorded levels, as the core makes them from
   syntax_foldexpr; the scrolling keys go through keys_feed as typed.
   The .txt is seed 1's 500 cases, then cases 155, 774, 1373 and 2172 of
   "test_core_fold.py 3000 9100", which plants need that no seed-1 case
   catches (a scrolldown cursor in a fold, a comment start on a fold's
   last line but one, update_topline's count to the last line).
   usage: test_core_fold [FILE] (default the .txt), -v prints every
   difference */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "fold.h"
#include "keys.h"
#include "syntax.h"
#include "../src/core/core_int.h"

/* A known difference, cursor column only: Neovim updates curswant
   lazily (w_set_curswant, VALID_VIRTCOL; validate_virtcol() is not fold
   aware where curs_columns() is), so once the cursor has been in a closed
   fold the column a later command lands on can differ from the core's,
   which sets it as the cursor lands.  The test allows it from the first
   time the cursor is in a closed fold until cursor() sets the column
   again; rows, Folded cells, the top line and the cursor line are still
   compared (measured: 3 of seed 1's 500 cases, 21 of seed 9100's 3000).
   A rule this wide could hide a column bug in a fold, so the recorded
   file's count of such screens is fixed: more, or fewer, fails */
enum { KNOWN_SCREENS = 6 };
static char line[1 << 16];
static int lv[4096], st[4096], nlv;

static int fe(void *ctx, size_t l, int *start)
{
	CHECK(((const Win *)ctx)->buf);   /* the window is alive (ASan) */
	*start = l < (size_t)nlv && st[l];
	return l < (size_t)nlv ? lv[l] : 0;
}

static void hex(const char *h, FILE *o, char *s)
{
	for (unsigned b; h[0] && h[1] && sscanf(h, "%2x", &b) == 1; h += 2)
		if (o) fputc((int)b, o);
		else *s++ = (char)b;
	if (s) *s = 0;
}

/* one recorded command, as keys or the core takes it */
static void run(Editor *e, const char *t)
{
	Win *w = e->win;
	size_t l;
	if (sscanf(t, "c %zu", &l) == 1) core_fold(e, FOLD_CLOSE, l, 1);
	else if (sscanf(t, "o %zu", &l) == 1) core_fold(e, FOLD_OPEN, l, 1);
	else if (!strcmp(t, "zM")) core_fold(e, FOLD_CLOSE_ALL, w->line, 1);
	else if (!strcmp(t, "zR")) core_fold(e, FOLD_OPEN_ALL, w->line, 1);
	else if (sscanf(t, "g %zu", &l) == 1) { core_goto(e, l, 0); return; }
	else {
		const char *p = t;
		long n = strtol(t, (char **)&p, 10);
		/* gj gk: the core's (keys' 'nowrap' k and j do not know folds yet) */
		if (!strcmp(p, "gj") || !strcmp(p, "gk")) { core_screen_move(e, (p[1] == 'j' ? 1 : -1) * (n ? n : 1)); return; }
		for (const char *d = t; d < p; d++) keys_feed(e, *d, 0);
		int k = !strcmp(p, "C-e") ? 5 : !strcmp(p, "C-y") ? 25 : !strcmp(p, "C-d") ? 4 : !strcmp(p, "C-u") ? 21
		      : !strcmp(p, "C-f") ? 6 : !strcmp(p, "C-b") ? 2 : 0;
		if (k) keys_feed(e, k, 0);
		else for (; *p; p++) keys_feed(e, *p, 0);
		return;
	}
	core_update(e);
}

static void feed(Editor *e, const char *k) { for (; *k; k++) keys_feed(e, (unsigned char)*k, 0); }

/* the core's wiring: no folds is E490, a split copies the folds, an edit
   moves them in every window (foldMarkAdjust()), and the rows core_folds()
   gets in Insert mode wait until it ends */
static void wiring(const char *file)
{
	static Editor e;
	size_t a = 0, z = 0;
	FILE *o = fopen(file, "w");
	CHECK(o && fputs("a\nb\nc\nd\ne\nf\n", o) >= 0 && !fclose(o));
	CHECK(!core_open(&e, file));
	CHECK(!e.win->fold && core_fold(&e, FOLD_CLOSE, 0, 1) == -1);
	nlv = 0;
	for (const char *p = "0 0 >1 1 1 0"; *p; p++)
		if (*p != ' ') { st[nlv] = *p == '>'; if (st[nlv]) p++; lv[nlv++] = *p - '0'; }
	CHECK((e.win->fold = fold_new(0, fe, win_tall, e.win)));
	fold_update(e.win->fold, 0, 6, 6);
	Win *w1 = e.win;
	CHECK(!core_split(&e, 0, 0) && e.win != w1 && e.win->fold && e.win->fold != w1->fold);
	CHECK(fold_closed(e.win->fold, 3, &a, &z) && a == 2 && z == 4);
	feed(&e, "ggdd");
	for (Win *w = e.wins; w; w = w->next) CHECK(fold_closed(w->fold, 2, &a, &z) && a == 1 && z == 3);
	CHECK(core_fold(&e, FOLD_OPEN, 1, 1) == 0 && !fold_closed(e.win->fold, 2, &a, &z));
	/* lines 0 and 1 now make a fold, which w1 (zc's not run there) shows
	   closed once Insert mode ends */
	st[0] = 1, lv[0] = lv[1] = 1;
	feed(&e, "i");
	core_folds(&e, e.buf, 0, 2);
	CHECK(!fold_closed(w1->fold, 0, &a, &z));
	feed(&e, "\x1b");
	core_update(&e);
	CHECK(fold_closed(w1->fold, 0, &a, &z) && a == 0 && z == 1);
	/* rows wait in Insert mode; a line split there moves every line under
	   it, so the rows reach the last line: at Esc a fold made far below is
	   seen */
	feed(&e, "ggi");
	core_folds(&e, e.buf, 0, 1);
	feed(&e, "\r");
	nlv = 6;
	for (int i = 0; i < nlv; i++) lv[i] = i >= 4, st[i] = i == 4;
	feed(&e, "\x1b");
	core_update(&e);
	for (Win *w = e.wins; w; w = w->next) CHECK(fold_level(w->fold, 3) == 0 && fold_level(w->fold, 5) == 1);
	/* cursor_up_inner(): a step up into a closed fold goes to its first
	   line, except the last step in Insert mode (the fold opens) */
	CHECK(fold_closed(w1->fold, 5, &a, &z) && e.wins == w1 && (e.win = w1));
	nlv = 7;   /* a line under the fold to step up from */
	lv[6] = st[6] = 0;
	feed(&e, "Go\x1b");
	core_update(&e);
	CHECK(core_line_step(&e, 6, -1) == 4);
	e.mode = MODE_INSERT;
	CHECK(core_line_step(&e, 6, -1) == 5 && core_line_step(&e, 6, -2) == 3);
	e.mode = MODE_NORMAL;
	/* the split's folds ask the split, not the window split from: with
	   that one closed, a read of it is a use after free under ASan */
	CHECK(!core_close(&e, 1) && e.win != w1);
	core_folds(&e, e.buf, 0, buf_lines(e.buf));
	CHECK(core_fold(&e, FOLD_CLOSE_ALL, 0, 1) == 0 && fold_closed(e.win->fold, 5, &a, &z));
	core_free(&e);
}

/* scroll_to_fraction() puts a window's top line back by rows; with the
   lines above in a closed fold the top is the fold's first line
   (set_topline()), in a window that is not the current one too */
static void fraction(const char *file)
{
	static Editor e;
	size_t a = 0, z = 0;
	FILE *o = fopen(file, "w");
	CHECK(o);
	for (int i = 0; i < 40; i++) fputs("x\n", o);
	CHECK(!fclose(o) && !core_open(&e, file));
	nlv = 40;
	for (int i = 0; i < nlv; i++) lv[i] = i >= 5 && i < 30, st[i] = i == 5;
	layout_resize(&e, 40, 80);
	CHECK((e.win->fold = fold_new(0, fe, win_tall, e.win)));
	fold_update(e.win->fold, 0, 40, 40);
	CHECK(fold_closed(e.win->fold, 10, &a, &z) && a == 5 && z == 29);
	core_goto(&e, 35, 0);
	core_update(&e);
	Win *w1 = e.win;
	CHECK(!core_split(&e, 0, 0) && e.win != w1);
	core_goto(&e, 0, 0);
	core_update(&e);
	core_resize(&e, 0, 3, 1);   /* the current window 3 rows: w1 grows */
	core_update(&e);
	CHECK(w1->h > 18 && w1->top == 5);
	/* the levels change while the buffer is hidden: entering it again
	   evaluates them anew (w_foldinvalid), the saved open/closed kept */
	char f2[80];
	snprintf(f2, sizeof f2, "%s2", file);
	CHECK((o = fopen(f2, "w")) && !fclose(o) && !core_edit_file(&e, f2, 0) && !e.win->fold);
	for (int i = 0; i < nlv; i++) lv[i] = i >= 30 && i < 36, st[i] = i == 30;
	CHECK(!core_buf_alt(&e) && e.win->fold);
	CHECK(!fold_closed(e.win->fold, 10, &a, &z) && fold_closed(e.win->fold, 32, &a, &z) && a == 30 && z == 35);
	unlink(f2);
	core_free(&e);
}

/* main.c's idle step: the parse, then its rows to the folds */
static void parse(Editor *e)
{
	size_t r1, r2;
	while (e->buf->syn) {
		int r = syntax_update(e->buf->syn, e->buf->text, NULL, &r1, &r2);
		if (r1 < r2) core_folds(e, e->buf, r1, r2);
		if (r != 2) break;
	}
}

/* which files get folds (win_folds()): an Org file's start closed below
   level 1, a Markdown file's open, a text file has none; a buffer left and
   entered again keeps its open and closed folds, and a new window takes
   those of the window that shows it (get_winopts()'s cloneFoldGrowArray();
   measured in nvim: a zo kept across :e and :b, and across :new and :b) */
static void filetypes(const char *dir)
{
	static Editor e;
	char org[64], md[64], txt[64];
	size_t a = 0, z = 0;
	snprintf(org, sizeof org, "%s/a.org", dir);
	snprintf(md, sizeof md, "%s/b.md", dir);
	snprintf(txt, sizeof txt, "%s/c.txt", dir);
	const char *const f[][2] = { { org, "* a\n** b\nx\n* c\n** d\ny\n" }, { md, "# a\n## b\nx\n" }, { txt, "# a\n## b\nx\n" } };
	for (int i = 0; i < 3; i++) {
		FILE *o = fopen(f[i][0], "w");
		CHECK(o && fputs(f[i][1], o) >= 0 && !fclose(o));
	}
	/* :e from the empty first buffer loads into it (do_ecmd()'s reuse) */
	CHECK(!core_open(&e, NULL) && !e.win->fold && !core_edit_file(&e, org, 0) && e.win->fold);
	parse(&e);
	CHECK(!fold_closed(e.win->fold, 0, &a, &z) && fold_closed(e.win->fold, 1, &a, &z) && a == 1 && z == 2);
	CHECK(fold_closed(e.win->fold, 4, &a, &z) && a == 4 && z == 5);
	CHECK(core_fold(&e, FOLD_OPEN, 1, 1) == 0 && !fold_closed(e.win->fold, 1, &a, &z));
	CHECK(!core_edit_file(&e, md, 0));
	parse(&e);
	CHECK(e.win->fold && fold_level(e.win->fold, 2) == 2 && !fold_closed(e.win->fold, 1, &a, &z));
	CHECK(!core_edit_file(&e, txt, 0));
	CHECK(!e.win->fold);
	CHECK(!core_buf_goto(&e, 1));
	CHECK(e.win->fold && !fold_closed(e.win->fold, 1, &a, &z) && fold_closed(e.win->fold, 4, &a, &z));
	/* and again: the copy saved before is replaced (a leak under ASan if not freed) */
	CHECK(!core_buf_alt(&e) && !core_buf_alt(&e));
	CHECK(e.win->fold && !fold_closed(e.win->fold, 1, &a, &z) && fold_closed(e.win->fold, 4, &a, &z));
	/* a new window takes them from the window that shows the buffer (measured) */
	CHECK(!core_new(&e, 0) && !core_buf_goto(&e, 1));
	CHECK(e.win->fold && !fold_closed(e.win->fold, 1, &a, &z) && fold_closed(e.win->fold, 4, &a, &z));
	core_free(&e);
}

/* an edit outside Insert mode updates the folds at once, with the levels
   it left (a new row -1), as changed_common()'s foldUpdate(); the parse's
   update follows.  Measured in nvim 0.12.6 (vim.treesitter.foldexpr,
   foldlevel 99, typed keys, 1 run): after "4Gzc 2Gzc 8Gox<Esc>dd" f's
   closed fold reaches over g() (2-11) until the parse splits them, both
   closed (2-8, 9-11) */
static void edits(const char *dir)
{
	static Editor e;
	char h[64];
	size_t a = 0, z = 0;
	snprintf(h, sizeof h, "%s/e.h", dir);
	FILE *o = fopen(h, "w");
	CHECK(o && fputs("int a;\nvoid f() {\n  int b;\n  if (b) {\n    b++;\n\n  }\n}\nvoid g() {\n  g();\n}\nint z;\n", o) >= 0 && !fclose(o));
	CHECK(!core_open(&e, h) && e.win->fold);
	parse(&e);
	CHECK(!core_fold(&e, FOLD_CLOSE, 3, 1) && !core_fold(&e, FOLD_CLOSE, 1, 1));
	e.win->line = 7;
	feed(&e, "ox\033");
	parse(&e);
	feed(&e, "dd");
	CHECK(fold_closed(e.win->fold, 1, &a, &z) && a == 1 && z == 10);
	parse(&e);
	CHECK(fold_closed(e.win->fold, 1, &a, &z) && a == 1 && z == 7);
	CHECK(fold_closed(e.win->fold, 8, &a, &z) && a == 8 && z == 10);
	core_free(&e);
	/* a join updates its line before the adjust (do_join()'s order):
	   "6G2zaJ" leaves g()'s first two lines a closed fold, its last open
	   (measured as above) */
	CHECK(!core_open(&e, h));
	parse(&e);
	e.win->line = 5;
	CHECK(!core_fold(&e, FOLD_TOGGLE, 5, 2));
	feed(&e, "J");
	parse(&e);
	CHECK(fold_closed(e.win->fold, 7, &a, &z) && a == 7 && z == 8 && !fold_closed(e.win->fold, 9, &a, &z));
	core_free(&e);
	/* a window moved to a frame keeps its folds (win_init()'s
	   copyFoldingState()), and every frame's windows free theirs: CTRL-W
	   T, then SPC 5 m back into the first frame (ASan's leak check) */
	CHECK(!core_open(&e, h));
	parse(&e);
	CHECK(!core_fold(&e, FOLD_CLOSE, 1, 1) && !core_split(&e, 0, 0) && !core_win_to_tab(&e, 0));
	CHECK(e.win->fold && fold_closed(e.win->fold, 1, &a, &z) && a == 1);
	CHECK(!core_split(&e, 0, 0) && !core_win_to_tab(&e, 1));
	CHECK(e.win->fold && fold_closed(e.win->fold, 1, &a, &z) && a == 1);
	core_free(&e);
	/* an edit adjusts and updates the folds of every frame's windows on
	   the buffer (FOR_ALL_TAB_WINDOWS in mark_adjust_internal() and
	   changed_common()), also the rows Insert mode kept back: a window
	   in another frame with the same folds closed keeps them equal to
	   the current window's after each step */
	CHECK(!core_open(&e, h));
	parse(&e);
	CHECK(!core_fold(&e, FOLD_CLOSE, 8, 1) && !core_fold(&e, FOLD_CLOSE, 1, 1) && !core_split(&e, 0, 0) && !core_win_to_tab(&e, 0));
	Win *other = tab_firstwin(&e, e.tabs) == e.win ? tab_firstwin(&e, e.tabs->next) : tab_firstwin(&e, e.tabs);
	CHECK(other != e.win && other->buf == e.buf);
	static const char *const steps[] = { "ggdd", "dd", "Gox\033", "ggyyp", "4Gox\ny\033", "u" };
	for (size_t i = 0; i < sizeof steps / sizeof *steps; i++)
		for (int p = 0; p < 2; p++) {
			if (p) parse(&e);
			else feed(&e, steps[i]), core_update(&e);
			for (size_t l = 0; l < core_lines(&e); l++) {
				size_t a2 = 0, z2 = 0;
				int c1 = fold_closed(e.win->fold, l, &a, &z), c2 = fold_closed(other->fold, l, &a2, &z2);
				CHECK(c1 == c2 && (!c1 || (a == a2 && z == z2)));
			}
		}
	/* Insert mode goes on when the OS focuses another frame (main.c's
	   core_tab_enter); <Esc> there brings the rows the first frame kept
	   back, though no window of its frame shows the buffer */
	char c[64];
	snprintf(c, sizeof c, "%s/e.c", dir);
	Tab *t1 = e.tab;
	CHECK(!core_edit_file(&e, c, 0) && e.win->buf != other->buf);
	core_tab_enter(&e, other == tab_firstwin(&e, e.tabs) ? e.tabs : e.tabs->next);
	feed(&e, "Gox");
	core_tab_enter(&e, t1);
	feed(&e, "\033");
	CHECK(other->frame->s.fend <= other->frame->s.ftop);
	core_free(&e);
	unlink(c);
	unlink(h);
}

int main(int argc, char **argv)
{
	static Editor e;
	const char *path = "tests/test_core_fold.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	char dir[] = "/tmp/core_foldXXXXXX", file[64];
	CHECK(mkdtemp(dir));
	snprintf(file, sizeof file, "%s/f", dir);
	wiring(file);
	fraction(file);
	filetypes(dir);
	edits(dir);
	FILE *f = fopen(path, "r");
	CHECK(f);
	int rows = 0, cols = 0, wrap = 0, level = 0, ncase = 0, bad = 0, differ = 0, r = 0, infold = 0, nkn = 0;
	size_t top = 0, cl = 0, cc = 0;
	static char want[4096], got[4096];
	Grid g = { 0 };
	const char *tok = "(start)";
	static char tbuf[sizeof line];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %d", &rows, &cols, &wrap, &level) == 4);
			differ = infold = 0;
		} else if (line[0] == 'L') {
			nlv = 0;
			for (char *p = strtok(line + 2, " "); p; p = strtok(NULL, " ")) {
				CHECK(nlv < 4096);
				int s = *p == '>';
				st[nlv] = s;
				lv[nlv++] = atoi(p + s);
			}
		} else if (line[0] == 'F') {
			FILE *o = fopen(file, "w");
			CHECK(o);
			hex(line + 2, o, NULL);
			CHECK(!fclose(o));
			CHECK(!core_open(&e, file) && !grid_resize(&g, rows, cols));
			layout_resize(&e, rows, cols);
			core_set_wrap(&e, wrap);
			size_t n = buf_lines(e.buf);
			CHECK(!e.win->fold && (e.win->fold = fold_new(level, fe, win_tall, e.win)));
			fold_update(e.win->fold, 0, n, n);
			core_set_want(&e);   /* after the folds, as core_open() */
			core_update(&e);
			tok = "(start)";
		} else if (line[0] == 'T') {
			snprintf(tbuf, sizeof tbuf, "%s", line + 2);
			tok = tbuf;
			if (tok[0] == 'g' && tok[1] == ' ') infold = 0;
			run(&e, tok);
		} else if (line[0] == 'W') {
			CHECK(sscanf(line, "W %zu %zu %zu", &top, &cl, &cc) == 3);
			core_draw(&e, NULL, &g);
			r = 0;
			const Win *w = e.win;
			size_t a, z;
			infold |= fold_closed(w->fold, w->line, &a, &z) != 0;
			nkn += w->top == top && w->line == cl && w->col != cc && infold;
			if (w->top != top || w->line != cl || (w->col != cc && !infold)) {
				if (!differ++ || verbose)
					fprintf(stderr, "case %d after %s: top %zu cursor %zu,%zu, nvim top %zu cursor %zu,%zu\n",
					        ncase, tok, w->top, w->line, w->col, top, cl, cc);
			}
		} else if (line[0] == 'R') {
			hex(line + 2, NULL, want);
			const Win *w = e.win;
			size_t k = 0;
			for (int c = 0; c < w->w && r < w->h; c++) {
				const Cell *x = &g.c[(w->y + r) * g.cols + w->x + c];
				if (!x->len) continue;
				memcpy(got + k, grid_text(x), x->len);
				k += x->len;
			}
			got[k] = 0;
			while (k && got[k - 1] == ' ') got[--k] = 0;
			for (size_t m = strlen(want); m && want[m - 1] == ' '; ) want[--m] = 0;
			if (strcmp(got, want) && (!differ++ || verbose))
				fprintf(stderr, "case %d after %s row %d:\n  nvim [%s]\n  core [%s]\n", ncase, tok, r, want, got);
		} else if (line[0] == 'A') {
			const Win *w = e.win;
			char a[512];
			int c = 0;
			for (; c < w->w && c < (int)sizeof a - 1 && r < w->h; c++)
				a[c] = g.c[(w->y + r) * g.cols + w->x + c].face == FACE_FOLDED ? 'F' : '.';
			/* known difference: the '>' of a double-width character cut at
			   a fold row's end is NonText, which Neovim draws Folded on some
			   screens (measured: folds on the first lines before the cursor
			   moves) and NonText on the rest */
			const Cell *x = c ? &g.c[(w->y + r) * g.cols + w->x + c - 1] : NULL;
			if (x && x->face == FACE_NON_TEXT && x->len == 1 && *grid_text(x) == '>')
				a[c - 1] = (int)strlen(line + 2) >= c ? line[2 + c - 1] : '.';
			while (c && a[c - 1] == '.') c--;
			a[c] = 0;
			if (strcmp(a, line + 2) && (!differ++ || verbose))
				fprintf(stderr, "case %d after %s row %d Folded:\n  nvim %s\n  core %s\n", ncase, tok, r, line + 2, a);
			r++;
		} else if (line[0] == 'E') {
			bad += differ > 0;
			core_free(&e);
			ncase++;
		}
	}
	fclose(f);
	grid_free(&g);
	unlink(file);
	rmdir(dir);
	printf("test_core_fold: %d cases, %d differ from Neovim (%d screens with the known column difference)\n", ncase, bad, nkn);
	if (!strcmp(path, "tests/test_core_fold.txt") && nkn != KNOWN_SCREENS) {
		printf("test_core_fold: %d screens with the known column difference, the recorded file has %d\n", nkn, KNOWN_SCREENS);
		return 1;
	}
	return bad != 0;
}
