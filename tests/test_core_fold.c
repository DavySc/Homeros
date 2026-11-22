/* Closed folds against Neovim (package 4): replays the cases that
   tests/test_core_fold.py recorded (a text, a fold level per line, a
   window, commands) and compares after each command the window's rows,
   which cells are Folded, the top line and the cursor.  The folds are
   fold.h's, from the recorded levels, as the core makes them from
   syntax_foldexpr; the scrolling keys go through keys_feed as typed.
   usage: test_core_fold [FILE] (default the .txt), -v prints every
   difference */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "fold.h"
#include "keys.h"
#include "../src/core/core_int.h"

/* known differences, cursor column only: Neovim updates curswant lazily
   (w_set_curswant, VALID_VIRTCOL), and in a closed fold its gk that fails
   on the first line (310, 481) and a CTRL-F after a fold toggled under the
   cursor (325) keep a column that the core, which sets it as the cursor
   lands, does not */
static const int known[] = { 310, 325, 481 };

static char line[1 << 16];
static int lv[4096], st[4096], nlv;

static int fe(void *ctx, size_t l, int *start)
{
	(void)ctx;
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
	core_free(&e);
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
	FILE *f = fopen(path, "r");
	CHECK(f);
	int rows = 0, cols = 0, wrap = 0, level = 0, ncase = 0, bad = 0, differ = 0, r = 0, iskn = 0;
	size_t top = 0, cl = 0, cc = 0;
	static char want[4096], got[4096];
	Grid g = { 0 };
	const char *tok = "(start)";
	static char tbuf[sizeof line];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %d", &rows, &cols, &wrap, &level) == 4);
			differ = iskn = 0;
			for (size_t k = 0; k < sizeof known / sizeof *known; k++) iskn |= known[k] == ncase;
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
			run(&e, tok);
		} else if (line[0] == 'W') {
			CHECK(sscanf(line, "W %zu %zu %zu", &top, &cl, &cc) == 3);
			core_draw(&e, &g);
			r = 0;
			const Win *w = e.win;
			if (w->top != top || w->line != cl || (w->col != cc && !iskn)) {
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
			a[c] = 0;
			/* known difference: the '>' of a double-width character cut at
			   a fold row's end is NonText, which Neovim draws Folded on some
			   screens (measured: folds on the first lines before the cursor
			   moves) and NonText on the rest */
			const Cell *x = c ? &g.c[(w->y + r) * g.cols + w->x + c - 1] : NULL;
			if (x && x->face == FACE_NON_TEXT && x->len == 1 && *grid_text(x) == '>') a[c - 1] = line[2 + c - 1];
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
	printf("test_core_fold: %d cases, %d differ from Neovim (%d known)\n", ncase, bad, (int)(sizeof known / sizeof *known));
	return bad != 0;
}
