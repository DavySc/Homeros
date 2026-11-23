/* Drawing the screen: windows (drawscreen.c win_update(), drawline.c
   win_line()), status lines (statusline.c build_stl_str_hl() with the
   default 'statusline'), the command line and the completion popup
   (popupmenu.c).  Allocates nothing. */
#include <stdio.h>
#include <string.h>
#include "core_int.h"
#include "git.h"
#include "lsp.h"
#include "syntax.h"
#include "uni.h"

/* A cluster into the grid.  One that starts with a character that would
   join a space is drawn on a space (utf_iscomposing_first(),
   schar_from_buf_first()); the grid cuts what is then too long. */
static void put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width)
{
	unsigned char b[1 + GRID_CLUSTER_MAX] = " ";
	if (s[0] >= 0x80) {
		size_t k = uni_len(s, n, 0);
		memcpy(b + 1, s, k);
		if (uni_glen(b, k + 1, 0) > 1) {
			if (n > GRID_CLUSTER_MAX) n = GRID_CLUSTER_MAX;
			memcpy(b + 1, s, n);
			s = b;
			n++;
		}
	}
	grid_put(g, r, c, s, n, width, ATTR_NONE);
}

/* The text area of window w: a cell put at window row r, column c, clipped
   to the window. */
static void wput(Grid *g, const Win *w, int r, int c, const unsigned char *s, size_t n, int width)
{
	if (c >= 0 && c + width <= w->w) put(g, w->y + r, w->x + c, s, n, width);
}

/* Window w's cell at row r, column c, or NULL past the window or the grid:
   a window can reach past a terminal that shrank (see grid_put). */
static Cell *wcell(Grid *g, const Win *w, int r, int c)
{
	int y = w->y + r, x = w->x + c;
	return c >= 0 && c < w->w && y >= 0 && y < g->rows && x >= 0 && x < g->cols ? &g->c[y * g->cols + x] : NULL;
}

static void wputs(Grid *g, const Win *w, int r, int c, const char *s, unsigned char face)
{
	for (; *s; s++, c++) {
		wput(g, w, r, c, (const unsigned char *)s, 1, 1);
		Cell *z = wcell(g, w, r, c);
		if (z) z->face = face;
	}
}

/* face on n cells of screen row r from column c */
static void paint(Grid *g, int r, int c, int n, unsigned char face)
{
	for (; n > 0 && r < g->rows && c < g->cols; n--, c++) if (c >= 0) g->c[r * g->cols + c].face = face;
}

/* Cell j of the character s (k bytes) that takes width cells: a tab is
   spaces, a control character ^X, an invalid byte or what cannot be
   printed in hex (transchar_hex()). */
static void put_cell(Grid *g, const Win *w, int r, int c, const unsigned char *s, size_t k, int j,
                     int width, unsigned char face, unsigned char bg, unsigned char ul)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char b;
	if (*s == '\t') b = ' ';
	else if (*s < 0x20 || *s == 0x7f) b = j ? (unsigned char)(*s == 0x7f ? '?' : *s + 0x40) : '^';
	else if (width >= 4) {
		/* <xx> or <xxxx>: width - 2 hex digits of the byte or code point */
		unsigned c = k == 1 ? *s : uni_cp(s, k, 0);
		for (int d = width - 2 - j; d > 0; d--) c /= 16;
		b = j == 0 ? '<' : j == width - 1 ? '>' : (unsigned char)hex[c % 16];
	}
	else if (j) return;
	else { b = 0; wput(g, w, r, c, s, k, width); }
	if (b) wput(g, w, r, c, &b, 1, 1);
	Cell *z = wcell(g, w, r, c);
	if (z) { z->face = face; z->bg = bg; z->ul = ul; }
}

/* vim.diagnostic with Neovim 0.12's defaults (signs, underline) and
   virtual_text = {current_line = true} (Davy's pick).  The diagnostics
   that start on line l, [*lo, *hi) of the array returned: lsp.h keeps
   them sorted by start, diagnostic_lines() groups them by lnum */
static const LspDiag *diags_on(const Win *w, size_t l, int *lo, int *hi)
{
	int n, a = 0, z;
	const LspDiag *v = lsp_diags(w->buf->lsp, &n);
	const Text *t = w->buf->text;
	/* past the last line: on it (vim.diagnostic.set() clamps the row) */
	size_t ls = text_line_start(t, l), le = l + 1 < text_lines(t) ? text_line_start(t, l + 1) : (size_t)-1;
	for (z = n; a < z; ) {
		int m = (a + z) / 2;
		if (v[m].a < ls) a = m + 1;
		else z = m;
	}
	for (z = a; z < n && v[z].a < le; z++) {}
	*lo = a, *hi = z;
	return v;
}

/* The bytes [*a, *z) d underlines: vim.hl.range() from (lnum, min(col,
   #line - 1)) to (end_lnum, end_col), each row clamped to the last line
   and each column to its line's end, the two swapped if the end comes
   first (an end past the last line can: measured), one character where
   they meet (getregionpos()) */
static void diag_span(const Text *t, const LspDiag *d, size_t *a, size_t *z)
{
	size_t nl = text_lines(t), al = text_line_of(t, d->a), zl = text_line_of(t, d->z);
	size_t ca = d->a - text_line_start(t, al), cz = d->z - text_line_start(t, zl);
	if (al >= nl) al = nl - 1;
	if (zl >= nl) zl = nl - 1;
	size_t s = text_line_start(t, al), n = text_line_start(t, al + 1) - 1 - s;
	size_t es = text_line_start(t, zl), en = text_line_start(t, zl + 1) - 1 - es;
	*a = s + (ca < n ? ca : n ? n - 1 : 0);
	*z = es + (cz < en ? cz : en);
	if (*z < *a) { size_t x = *a; *a = *z; *z = x; }
	else if (*z == *a) (*z)++;
}

/* 0 error .. 3 hint, for the FACE_DIAG_* of each kind */
static int sev(const LspDiag *d) { return d->sev < 4 ? d->sev - 1 : 3; }

/* The number column of line l (get_line_number_str()): relative to the
   cursor line, which shows its own number left-aligned, in CursorLineNr
   ('cursorline', get_line_number_attr()).  Returns the column the "<<<"
   marker reaches (0: none), which the text leaves. */
static int put_number(Grid *g, const Win *w, int r, size_t l, int top_skipped)
{
	char b[40];
	int sg = win_signw(w), nw = win_numw(w) - sg - 1;
	size_t rel = l > w->line ? l - w->line : w->line - l;
	unsigned char face = rel ? FACE_LINE_NR : FACE_CURSOR_LINE_NR;
	*b = 0;
	if (nw > 0) {
		snprintf(b, sizeof b, rel ? "%*zu " : "%-*zu ", nw, rel ? rel : l + 1);
		wputs(g, w, r, sg, b, face);
	}
	/* the sign column: the last diagnostic's sign of those that start on
	   the line (equal priorities: the newest extmark; measured) */
	int lo, hi;
	const LspDiag *v = sg ? diags_on(w, l, &lo, &hi) : NULL;
	if (v && hi > lo) {
		char sign[3] = { "EWIH"[sev(&v[hi - 1])], ' ', 0 };
		wputs(g, w, r, 0, sign, (unsigned char)(FACE_DIAG_ERROR + sev(&v[hi - 1])));
	}
	if (!top_skipped) return 0;
	/* skipcol hides the line's start: "  5" becomes "--5" and three '<'
	   follow the digits, over the text's first cells when the digits fill
	   the column (win_line(), draw_lnum_col(); measured on lines 9, 10, 100) */
	int d = 0;
	for (; b[d] == ' '; d++) wputs(g, w, r, sg + d, "-", face);
	while (b[d] >= '0' && b[d] <= '9') d++;
	/* after the row's first digits: none where a sign column starts it,
	   so over it, nor with nonumber: over the text's first cells (measured) */
	if (sg) d = -sg;
	wputs(g, w, r, sg + d, "<<<", FACE_NON_TEXT);
	return sg + d + 3;
}

static int line_rows(const Win *w, size_t l)
{
	int tw = w->w - win_numw(w);
	if (!w->wrap || tw <= 0) return 1;
	size_t v = win_vcol_end(w, l), skip = l == w->top ? w->skipcol : 0;
	return v <= skip ? 1 : (int)((v - skip + (size_t)tw - 1) / (size_t)tw);
}

/* virtual_text after a line's end and the blank there (eol virt_text),
   as _get_virt_text_chunks() lays out the diagnostics [lo, hi) of v: 4
   blanks, a '■' in each one's colour, a blank and the last one's message,
   its "\r" dropped, each "\n" two blanks, each Tab blanks to the row's
   next multiple of 8 (measured); cut at the window's edge.
   The blanks have no colour of their own (hl_mode combine: on CursorLine) */
/* grid_puts() for virtual text: a double-width character cut at the edge
   leaves a blank in its colour (measured) */
static int vt_puts(Grid *g, int y, int x, int end, const char *s, size_t n, int f)
{
	int z = grid_puts(g, y, x, end, s, n, ATTR_NONE, f);
	return z < end && x + grid_width(s, n) > end ? grid_puts(g, y, z, end, " ", 1, ATTR_NONE, f) : z;
}

static void draw_vt(Grid *g, const Win *w, int row, int nw, int c, const LspDiag *v, int lo, int hi, unsigned char cl)
{
	int y = w->y + row, end = w->x + w->w < g->cols ? w->x + w->w : g->cols, x = w->x + nw + c;
	if (y >= g->rows || x >= end) return;
	for (int z = grid_puts(g, y, x, end, "    ", 4, ATTR_NONE, FACE_NONE); x < z; x++) g->c[y * g->cols + x].bg = cl;
	for (int k = lo; k < hi; k++) x = vt_puts(g, y, x, end, "\xe2\x96\xa0", 3, FACE_DIAG_VT_ERROR + sev(&v[k]));
	int f = FACE_DIAG_VT_ERROR + sev(&v[hi - 1]);
	x = vt_puts(g, y, x, end, " ", 1, f);
	for (const char *m = v[hi - 1].msg; m && *m; ) {
		size_t n = strcspn(m, "\r\n\t");
		x = vt_puts(g, y, x, end, m, n, f);
		if (m[n] == '\n') x = vt_puts(g, y, x, end, "  ", 2, f);
		if (m[n] == '\t') x = vt_puts(g, y, x, end, "        ", (size_t)(8 - (x - w->x - nw) % 8), f);
		m += n + (m[n] != 0);
	}
}

/* An overlay (virt_text_pos 'overlay', win_line()) of the clusters s[0, n),
   reps times, at screen column v of line l from window row r, the overlay
   starting at s0: drawn from the view's first column when s0 is before it
   (leftcol, skipcol; measured: leftcol 1 to 39, the review of PR 56), cut
   at the end of s0's row (measured: the rest of a heading marker that wraps
   stays as typed), under skipcol's "<<<".  Its cells take face and bg, as
   hl_mode 'replace' (measured: a bullet's '.' over a heading's background
   has none).  Returns the column after it. */
static size_t over(Grid *g, const Win *w, int r, size_t skip, int cover, size_t s0, size_t v,
                   const char *s, size_t n, size_t reps, unsigned char face, unsigned char bg)
{
	const unsigned char *u = (const unsigned char *)s;
	int nw = win_numw(w), tw = w->w - nw;
	size_t d = skip > s0 ? skip - s0 : 0, x0 = s0 + d - skip;
	if (s0 == (size_t)-1) return v;   /* a mark past the window's end */
	for (; reps; reps--)
		for (size_t i = 0, k; i < n; i += k) {
			/* file text (an info string) as draw_line draws it: a tab as
			   blanks, a control character as ^X, an invalid byte as <xx>
			   (measured: Neovim's overlay does the same) */
			k = uni_glen(u, n, i);
			int cw = core_cellw(w, u, n, i, v);
			cw -= cw == 3 && u[i] != '\t';
			/* a character that does not fit the row's end: blanks there
			   (measured) */
			size_t e = v + d - skip + (size_t)cw - 1;
			int fit = e / (size_t)tw == x0 / (size_t)tw && (w->wrap || e < (size_t)tw);
			for (int j = 0; j < cw; j++, v++) {
				size_t x = v + d - skip;
				if (x / (size_t)tw != x0 / (size_t)tw || (!w->wrap && x >= (size_t)tw)) return v;
				int row = r + (int)(x / (size_t)tw), c = nw + (int)(x % (size_t)tw);
				if (row >= w->h || (row == r && c < cover)) continue;
				if (fit) put_cell(g, w, row, c, u + i, k, j, cw, face, bg, 0);
				else put_cell(g, w, row, c, (const unsigned char *)" ", 1, 0, 1, face, bg, 0);
			}
		}
	return v;
}

/* Background bg (hl_eol) on line l's cells from screen column b0 to the
   window's edge, on every row, but a '>' filler cell (NonText) an overlay
   left: win_line() gives it extra_attr alone (measured: a heading's and a
   code block's; a '<' takes a code block's) */
static void bar(Grid *g, const Win *w, size_t l, int r, size_t skip, int cover, size_t b0, unsigned char bg)
{
	int nw = win_numw(w), tw = w->w - nw, rows = line_rows(w, l);
	for (int k = 0; k < rows && r + k < w->h; k++)
		for (int c = nw; c < w->w; c++) {
			Cell *z = wcell(g, w, r + k, c);
			if (skip + (size_t)k * (size_t)tw + (size_t)(c - nw) >= b0 && z && !(k == 0 && c < cover) && !(z->face == FACE_NON_TEXT && z->ch[0] == '>'))
				z->bg = bg;
		}
}

/* The first byte of b[i, n) that is not a blank or a tab, or n */
static size_t blanks(const char *b, size_t n, size_t i)
{
	while (i < n && (b[i] == ' ' || b[i] == '\t')) i++;
	return i;
}

/* What render-markdown 8.14 draws over line l (Davy's Neovim, its
   defaults) at conceallevel 0: what it conceals is not drawn yet.  Screen
   columns: a0 of byte md->mark, a1 of the first non-blank from it, b1 of
   md->item, t0 of md->task_col. */
static void draw_md(Grid *g, const Win *w, size_t l, int r, size_t skip, int cover, const SyntaxMd *md,
                    size_t a0, size_t a1, size_t b1, size_t t0, unsigned char cl)
{
	char b[256];
	size_t n = text_read(w->buf->text, text_line_start(w->buf->text, l), b, sizeof b), v;
	/* the backgrounds first: an overlay's cells take its own */
	unsigned char hb = (unsigned char)(FACE_HEADING_1_BG + md->head - 1);
	if (md->head) bar(g, w, l, r, skip, cover, a0, hb);
	if (md->code == 2) bar(g, w, l, r, skip, cover, a0, FACE_MD_CODE_BG);
	if (md->task) {
		/* checkbox.lua: the icon and its blank, right_pad 1, hl_mode
		   'combine': on the cursor line's background (measured) */
		static const char *const icon[] = { "\xf3\xb0\x84\xb1 ", "\xf3\xb0\xb1\x92 ", "\xf3\xb0\xa5\x94 " };
		int k = md->task == ' ' ? 0 : md->task == 'x' ? 1 : 2;
		over(g, w, r, skip, cover, t0, over(g, w, r, skip, cover, t0, t0, icon[k], 5, 1,
		     k == 0 ? FACE_MARKUP_LIST_UNCHECKED : k == 1 ? FACE_MARKUP_LIST_CHECKED : FACE_MARKUP_RAW, cl),
		     " ", 1, 1, FACE_NONE, cl);
	} else if (md->list) {
		/* bullet.lua: from the marker on, a blank per blank character it
		   starts with (a tab too: str.spaces), then the icon by nesting
		   level or, ordered (ordered_icons), the marker's number if above
		   1, else the item's ordinal, and '.'; one wider than the marker
		   is inline text there, before the marker, which the core does not
		   draw yet: it is left as typed rather than hide the item's text */
		char o[24];
		size_t q = blanks(b, n, md->item), x = 0;
		for (size_t j = q; j < n && b[j] >= '0' && b[j] <= '9'; j++) x = x * 10 + (size_t)(b[j] - '0');
		static const char icon[] = "\xe2\x97\x8f\xe2\x97\x8b\xe2\x97\x86\xe2\x97\x87";
		size_t k = md->ordered ? (size_t)snprintf(o, sizeof o, "%zu.", x > 1 ? x : md->index) : 3;
		if (!md->ordered || k <= md->iend - q) over(g, w, r, skip, cover, b1, over(g, w, r, skip, cover, b1, b1, " ", 1, q - md->item, FACE_MD_BULLET, 0),
		     md->ordered ? o : icon + (md->list - 1) % 4 * 3, k, 1, FACE_MD_BULLET, 0);
	}
	/* after the marker's, over it where both start before the view
	   (measured) */
	if (md->head) {
		/* heading.lua, position 'overlay', width 'full': the indent and
		   the n '#' and the cell after them become blanks, the level's icon
		   (nf-md-numeric_1_circle_outline to 6: U+F0CA1 to U+F0CAB by twos)
		   and a blank, on the level's background */
		unsigned char f = (unsigned char)(FACE_MARKUP_HEADING_1 + md->head - 1);
		const char icon[] = { '\xf3', '\xb0', '\xb2', (char)(0xa1 + 2 * (md->head - 1)) };
		v = over(g, w, r, skip, cover, a0, a0, " ", 1, a1 - a0 + md->head - 1, f, hb);
		over(g, w, r, skip, cover, a0, over(g, w, r, skip, cover, a0, v, icon, 4, 1, f, hb), " ", 1, 1, f, hb);
	}
	if (md->code == 1 && md->nlang && md->lang + md->nlang <= n) {
		/* code.lua, language(): from the fence on, a '█' per blank
		   character it starts with (str.spaces), the
		   info string (the language as Normal, the rest as @label) on
		   RenderMarkdownCode's background, then '█' to the row's end */
		const char *blk = "\xe2\x96\x88";
		v = over(g, w, r, skip, cover, a0, a0, blk, 3, blanks(b, n, md->mark) - md->mark, FACE_MD_CODE_BORDER, 0);
		v = over(g, w, r, skip, cover, a0, v, b + md->info, md->lang - md->info, 1, FACE_LABEL, FACE_MD_CODE_BG);
		v = over(g, w, r, skip, cover, a0, v, b + md->lang, md->nlang, 1, FACE_NONE, FACE_MD_CODE_BG);
		/* (the row ends before b's 256 bytes do, but in a wider window) */
		size_t e = md->info + md->ninfo < n ? md->info + md->ninfo : n;
		v = over(g, w, r, skip, cover, a0, v, b + md->lang + md->nlang, e - md->lang - md->nlang, 1, FACE_LABEL, FACE_MD_CODE_BG);
		over(g, w, r, skip, cover, a0, v, blk, 3, (size_t)w->w, FACE_MD_CODE_BORDER, 0);
	}
}

/* win_line()'s cell after a line's end, d columns into the view from
   row r: a blank there, or the last cell of the row the line ends on when
   the end is past the window's edge */
static Cell *eol_cell(Grid *g, const Win *w, int r, size_t d, int tw, int nw)
{
	int full = w->wrap ? d && d % (size_t)tw == 0 : d >= (size_t)tw;
	d -= (size_t)full;
	int row = w->wrap ? r + (int)(d / (size_t)tw) : r, c = nw + (int)(d % (size_t)tw);
	if (!full) wput(g, w, row, c, (const unsigned char *)" ", 1, 1);
	return row < w->h ? wcell(g, w, row, c) : NULL;
}

/* keys_hl's match of line l that holds byte i or comes after it (an empty
   one at i), from the match [*ms, *mz) found before; 0 when none */
static int hl_at(Editor *e, const Win *w, size_t l, size_t i, int mh, size_t *ms, size_t *mz)
{
	while (mh && (*mz > *ms ? i >= *mz : i > *ms)) mh = keys_hl(e, w, l, *mz > *ms ? *mz : i, ms, mz);
	return mh;
}

/* win_line()'s Visual area of line l in w ('selection' inclusive), or
   vim.hl.on_yank()'s flash (Davy's my-nvim): vim.hl.range() from '[ to ']
   inclusive (a block: its screen columns) in IncSearch, over the text only
   (no hl_eol) and only in the current window (the one it was yanked in:
   the flash is gone before another can be entered).  :s's c flag shows
   its match (ks.inc) the same way.  Cells [*from, *to),
   none if *from >= *to, less the cursor's [*xf, *xt], which Visual's block
   cursor shows (noinvcur: its cell, both of a double-width character's).
   Windows on the current buffer show the Visual area too.  Returns the
   area's face. */
static unsigned char visual(Editor *e, Win *w, size_t l, size_t *from, size_t *to, size_t *xf, size_t *xt)
{
	const Win *cw = e->win;
	const KeyArea *ka = e->ks.yank.type ? &e->ks.yank : e->ks.inc.type ? &e->ks.inc : NULL;
	int y = ka != NULL, type = y ? ka->type : e->ks.visual;
	size_t cl = y ? ka->l2 : cw->line, cc = y ? ka->c2 : cw->col, vl = y ? ka->l1 : e->ks.vline,
	       vc = y ? ka->c1 : e->ks.vcol, a, b, c, d, n;
	*from = *xf = 1;
	*to = *xt = 0;
	if (!type || (y ? w != cw : w->buf != e->buf)) return 0;
	/* ltoreq(): the cursor is the top end unless VIsual is before it */
	int vtop = vl < cl || (vl == cl && vc < cc);
	size_t tl = vtop ? vl : cl, tc = vtop ? vc : cc, bl = vtop ? cl : vl, bc = vtop ? cc : vc;
	if (l < tl || l > bl) return 0;
	if (type == 22 && y) {
		/* a yanked block: its screen columns (the register's width) */
		*from = vc;
		*to = cc + 1;
	} else if (type == 22) {
		/* block: w_old_cursor_fcol and _lcol (getvcols()), to the line's
		   end after $ */
		win_vcols(w, cl, cc, &a, &b);
		win_vcols(w, vl, vc, &c, &d);
		*from = a < c ? a : c;
		*to = cw->want == WANT_EOL ? (size_t)-1 : (b > d ? b : d) + 1;
	} else {
		*from = 0;
		*to = (size_t)-1;
		if (l == tl && type == 'v') {
			win_vcols(w, tl, tc, from, &b);
			core_line(e, tl, &n);
			if (tc >= n) *to = *from + 1;
		}
		if (l == bl && type == 'v') {
			win_vcols(w, bl, bc, &a, &b);
			*to = b + 1;
		}
	}
	if (y) {
		/* a block's extmarks cover whole characters (getregionpos()
		   bytes): every character its columns touch */
		LineIt it;
		const unsigned char *s;
		size_t f = *from, t = *to, v = 0, k, n2;
		lit_init(&it, w->buf, l);
		for (; type == 22 && (k = lit_next(&it, &s)); v += n2) {
			n2 = (size_t)core_cellw(w, s, k, 0, v);
			if (v < t && v + n2 > f) {
				if (v < *from) *from = v;
				if (v + n2 > *to) *to = v + n2;
			}
		}
		return FACE_INC_SEARCH;
	}
	if (w == cw && l == cw->line) {
		*xf = win_vcol(e, w, NULL, xt);
		const unsigned char *t = core_line(e, cw->line, &n);
		if (cw->col >= n || t[cw->col] < 0x80 || uni_width(t, n, cw->col) < 2) *xt = *xf;
	}
	return FACE_VISUAL;
}

/* Line l from window row r: in rows of the text width with wrap (the top
   line from skipcol), else one row from leftcol.  rm: render-markdown draws
   the line (2), or only what its anti_conceal leaves on the cursor line,
   a code block's background (1; anti_conceal.ignore), or nothing (0; see
   draw_win).  Returns 1 if the line was drawn to its
   end. */
static int draw_line(Editor *e, Grid *g, Win *w, size_t l, int r, int rm)
{
	int nw = win_numw(w), tw = w->w - nw;
	size_t skip = w->wrap ? (l == w->top ? w->skipcol : 0) : w->left;
	int cover = put_number(g, w, r, l, w->wrap && skip && l == w->top);
	/* no room for text: with wrap, win_line() wraps the number column itself
	   and the line takes the rest of the window */
	if (tw <= 0) return !w->wrap;
	LineIt it;
	const unsigned char *s;
	size_t k, v = 0, i = 0, ls = text_line_start(w->buf->text, l), f0 = (size_t)-1;
	unsigned char fc[256];          /* faces of bytes [f0, f0 + 256) of the line */
	struct Syntax *syn = w->buf->syn;
	/* drawn: 1 to the line's end, 0 cut at the window's bottom, 2 cut at its
	   right edge.  Of render-markdown's marks (draw_md): screen columns a0,
	   a1, b1, t0; one past the window marks nothing it shows */
	SyntaxMd md = { 0 };
	int drawn = 1, on = rm && syntax_md(syn, w->buf->text, l, &md);
	size_t a0 = (size_t)-1, a1 = a0, b1 = a0, t0 = a0;
	size_t from, to, xf, xt;
	unsigned char area = visual(e, w, l, &from, &to, &xf, &xt);
	/* 'hlsearch' (win_line()'s search_attr, under the Visual area and
	   :s's c-flag match but over the yank flash, an extmark: measured):
	   the keys' matches [ms, mz) from col 0 on, an empty one over the
	   character at ms; CurSearch for one that holds w's cursor (keys_hl's 2) */
	int flash = area && e->ks.yank.type;
	size_t ms = 0, mz = 0;
	int mh = keys_hl(e, w, l, 0, &ms, &mz);
	/* conceal (win_line(): conceallevel=2, concealcursor=""): spans drawn
	   as one character or none, but not on the current window's cursor
	   line nor in the Visual area; off counts the cells they freed on row
	   orow, which a Tab gives back (fix_for_boguscols()) */
	Conceal cs[64];
	size_t nc = syn && w->conceal && !(w == e->win && l == w->line) && area != FACE_VISUAL ? syntax_conceal(syn, l, cs, 64) : 0,
	       ci = 0, off = 0;
	int orow = r;
	/* the diagnostics' underline: [a, z) of those over the line
	   (diag_span()).  Of those over a byte the one that starts last wins,
	   of two that start together the earlier: equal extmark priorities,
	   so the order decor_redraw_line() meets them in (the marktree puts
	   a newer mark before an older one at its position; measured: with
	   more at one place its B-tree's order, not copied, see
	   tests/test_core_diag.py).  Kept: the last 64 */
	struct { size_t a, z; unsigned char f; } us[64];
	int nu = 0, nd;
	const LspDiag *dv = lsp_diags(w->buf->lsp, &nd);
	const Text *t = w->buf->text;
	/* (one past the last line is on it: diag_span()) */
	size_t le = text_line_start(t, l + 1), lim = l + 1 < text_lines(t) ? le : (size_t)-1;
	for (size_t k = 0; k < (size_t)nd && dv[k].a < lim; k++) {
		size_t a, z;
		if (dv[k].z < ls) continue;	/* ends before this line: skip without diag_span's walk */
		diag_span(t, &dv[k], &a, &z);
		if (a + 1 >= le || z <= ls || z <= a) continue;
		if (nu == 64) memmove(us, us + 1, --nu * sizeof *us);
		us[nu].a = a, us[nu].z = z, us[nu++].f = (unsigned char)(FACE_DIAG_UNDER_ERROR + sev(&dv[k]));
	}
	/* 'cursorline' (Davy's pick of the UI proposals): CursorLine under the
	   text area of the cursor line's rows, below syntax and render-markdown
	   (low priority: it has no fg), not in the current window in Visual
	   mode (win_line()); not under skipcol's "<<<" */
	unsigned char cl = l == w->line && !(w == e->win && e->ks.visual) ? FACE_CURSOR_LINE : 0;
	for (int k = 0; cl && k < line_rows(w, l) && r + k < w->h; k++)
		for (int c = k || cover < nw ? nw : cover; c < w->w; c++) {
			Cell *z = wcell(g, w, r + k, c);
			if (z) z->bg = cl;
		}
	lit_init(&it, w->buf, l);
	while ((k = lit_next(&it, &s))) {
		int cw = core_cellw(w, s, k, 0, v);
		if (syn && (f0 == (size_t)-1 || i + k > f0 + sizeof fc)) {
			size_t z = text_line_start(w->buf->text, l + 1) - 1;
			f0 = i;
			syntax_faces(syn, ls + f0, ls + f0 + sizeof fc < z ? ls + f0 + sizeof fc : z, fc);
		}
		if (i == md.mark) a0 = v;
		if (i >= md.mark && a1 == (size_t)-1 && *s != ' ' && *s != '\t') a1 = v;
		if (i == md.item) b1 = v;
		if (i == md.task_col) t0 = v;
		if (drawn != 1) break;
		/* a double-width character: after the '>' of a wrapping row's
		   last cell (core_cellw's 3), the two cells of the character */
		int lead = cw == 3 && *s != '\t', wide = *s >= 0x80 && cw - lead == 2;
		mh = hl_at(e, w, l, i, mh, &ms, &mz);
		int im = mh && i >= ms;
		unsigned char sb = im ? mh == 2 ? FACE_CUR_SEARCH : FACE_SEARCH : cl;
		while (ci < nc && i >= cs[ci].z) ci++;
		const Conceal *cc = ci < nc && i >= cs[ci].a ? &cs[ci] : NULL;
		int u = -1;
		for (int j = 0; j < nu; j++)
			if (us[j].a <= ls + i && ls + i < us[j].z && (u < 0 || us[j].a > us[u].a)) u = j;
		unsigned char ul = u < 0 ? 0 : us[u].f;
		for (int j = 0; j < cw; j++, v++) {
			if (v < skip) continue;
			size_t d = v - skip;
			int row = w->wrap ? r + (int)(d / (size_t)tw) : r;
			if (row != orow) orow = row, off = 0;
			if (!w->wrap && d - off >= (size_t)tw) { drawn = 2; break; }
			if (row >= w->h) { drawn = 0; break; }
			int c = nw + (int)(w->wrap ? d % (size_t)tw : d) - (int)off;
			unsigned char face = syn ? fc[i - f0] : 0;
			/* a double-width character whose second cell starts the area
			   is in it (win_line()) */
			unsigned char vis = (v >= from || (wide && v + 1 == from)) && v < to && (v < xf || v > xt) && !(flash && im) ? area : 0;
			/* the span's character in its first byte's first cell.  The
			   Org syntax conceals no double-width character (measured:
			   test_core_conceal.py), so Neovim's fillers around a
			   concealed one are not copied */
			if (cc) {
				if (i == cc->a && !j && *cc->s) put_cell(g, w, row, c, (const unsigned char *)cc->s, strlen(cc->s), 0, 1, face, vis ? vis : sb, ul);
				else off++;
				continue;
			}
			for (; *s == '\t' && off; off--) put_cell(g, w, row, c++, s, k, 0, cw, face, vis ? vis : sb, ul);
			if (row == r && c < cover) {
				/* "<<<" over a double-width character's first cell leaves a
				   blank in its second (measured) */
				if (wide && j == lead && c + 1 == cover) put_cell(g, w, row, c + 1, (const unsigned char *)" ", 1, 0, 1, face, vis ? vis : sb, ul);
				continue;
			}
			/* win_line(): '>' where a double-width character does not fit
			   (the line's end without wrap), '<' where the view starts in
			   its second half (MB_FILLER_CHAR), both in NonText (HLF_AT,
			   which the core draws dim, as '~') */
			/* '>' is NonText alone, off CursorLine; '<' on it (multi_attr;
			   measured) */
			if (j < lead || (wide && j == 0 && !w->wrap && d - off == (size_t)tw - 1))
				wputs(g, w, row, c, ">", FACE_NON_TEXT);   /* not in the area (measured) */
			else if (wide && j - lead == 1 && v == skip) {
				/* in the area if its character is (measured) */
				wputs(g, w, row, c, "<", FACE_NON_TEXT);
				Cell *z = wcell(g, w, row, c);
				if (z) z->bg = from < v && vis ? vis : sb;
			} else put_cell(g, w, row, c, s, k, j - lead, cw - lead, face, vis ? vis : sb, ul);
		}
		i += k;
	}
	/* win_line(): "Invert at least one char" where the area starts at the
	   line's end (a blank cell; in block mode only on the lines of its two
	   ends), or the last cell when the end is past the window's edge.  A
	   line that ends before the view starts has its end at the view's
	   start, where an area that starts earlier starts too. */
	size_t vs = v > skip ? v : skip;
	if (area == FACE_VISUAL && drawn == 1 && (v > skip ? from == v : from <= skip) && vs < to && (vs < xf || vs > xt)
	    && (e->ks.visual != 22 || l == e->win->line || l == e->ks.vline)) {
		Cell *z = eol_cell(g, w, r, vs - skip, tw, nw);
		if (z) z->bg = FACE_VISUAL;
	}
	/* a match that takes the line break: the cell after the last
	   character */
	mh = hl_at(e, w, l, i, mh, &ms, &mz);
	/* (one that takes the line break at the view's start when the line
	   ends before it, as the Visual area; an empty one with wrap only
	   where the end is in view, without wrap where 'leftcol' does not
	   pass the line's length in bytes, also when its end, in columns, is
	   before the view (a wide character) or in it (a Tab): measured) */
	if (mh && ms <= i && drawn == 1 && (mz > ms || (w->wrap ? v >= skip : w->left <= i))) {
		Cell *z = eol_cell(g, w, r, vs - skip, tw, nw);
		/* CurSearch only for a match with a character on this line, so
		   never on an empty one (get_search_match_hl(): measured) */
		if (z && z->bg != FACE_VISUAL) z->bg = mh == 2 && ms < i ? FACE_CUR_SEARCH : FACE_SEARCH;
	}
	/* a code block's empty or short row: its background from the block's
	   column (padding() pads it) */
	if (a0 == (size_t)-1) a0 = v + md.mark - i;
	if (rm == 1) md.head = md.list = md.task = 0, md.code = md.code == 2 ? 2 : 0;
	if (on) draw_md(g, w, l, r, skip, cover, &md, a0, a1, b1, t0, cl);
	/* the diagnostics' virtual text on the cursor line (CursorMoved
	   renders it there; in another buffer's window, its cursor line) */
	int lo, hi;
	dv = diags_on(w, l, &lo, &hi);
	if (drawn == 1 && hi > lo && l == (w->buf == e->win->buf ? e->win->line : w->line)) {
		size_t d = vs - skip;
		int row = w->wrap ? r + (int)(d / (size_t)tw) : r;
		if (row < r + line_rows(w, l) && row < w->h) draw_vt(g, w, row, nw, (int)(w->wrap ? d % (size_t)tw : d) + 1, dv, lo, hi, cl);
	}
	return drawn != 0;
}

/* win_update(): the lines from the top, "@@@" where the last one does not
   fit (display=lastline), "~" below the end of the buffer */
static void draw_win(Editor *e, Grid *g, Win *w)
{
	size_t n = buf_lines(w->buf), l = w->top;
	int r = 0;
	/* render-markdown 8.14 draws a buffer in normal and command-line mode
	   (render_modes; visual mode is normal mode with a selection here) while
	   the current window is not scrolled sideways (Updater:run), and leaves
	   the current window's cursor line as typed in every window on its
	   buffer (anti_conceal).  Another buffer keeps what it drew while it was
	   current: rendered unless its window is scrolled sideways. */
	int cur = w->buf == e->buf, rm = cur ? e->mode != MODE_INSERT && !e->ks.visual && !e->win->left : !w->left;
	size_t raw = cur ? e->win->line : (size_t)-1;
	for (; r < w->h && l < n; l++) {
		int rows = line_rows(w, l);
		if (!draw_line(e, g, w, l, r, rm ? 1 + (l != raw) : 0)) {
			/* a double-width character the "@@@" starts in becomes '@' too
			   (measured: four '@' after one) */
			int c0 = w->w - 3 < 0 ? 0 : w->w - 3, y = w->y + w->h - 1, x = w->x + c0;
			if (c0 > 0 && y < g->rows && x < g->cols && !g->c[y * g->cols + x].len) c0--;
			if (l != w->top)
				for (int c = c0; c < w->w; c++) wputs(g, w, w->h - 1, c, "@", FACE_NON_TEXT);
			return;
		}
		r += rows;
	}
	for (; r < w->h; r++) wputs(g, w, r, 0, "~", FACE_END_OF_BUFFER);
}

/* stl_connected(): the status line runs on into the one to its right */
static int stl_connected(const Win *w)
{
	for (const struct Frame *f = w->frame; f->parent; f = f->parent) {
		if (f->parent->layout == FR_COL) { if (f->next) break; }
		else if (f->next) return 1;
	}
	return 0;
}

/* the status line's text, each byte with its face and the face its
   background comes from (Cell.bg) */
typedef struct { char o[4400]; unsigned char f[4400], l[4400]; size_t n; } Stl;

static void stl_add(Stl *t, const char *s, unsigned char face, unsigned char layer)
{
	size_t k = strlen(s);
	if (k > sizeof t->o - t->n) k = sizeof t->o - t->n;
	memcpy(t->o + t->n, s, k);
	memset(t->f + t->n, face, k);
	memset(t->l + t->n, layer, k);
	t->n += k;
}

/* lualine with catppuccin's lualine theme, set up as UI proposal 5's
   mockup (tests/test_core_status.lua), laid out by build_stl_str_hl() in
   the window's width.  The current window:
   (mode)%<( ⎇ branch )( name ●)%=( type )·( utf-8 )(location)( progress)
   with the mode and the progress as pills between rounded ends (U+E0B6,
   U+E0B4, transitional groups: the end's fg is the section's bg) and the
   location on surface0; the others lualine's inactive sections,
   "%< name ●%=location".  Each component is padded with a space either
   side.  The current window's also shows the branch before the name and
   the file type before the encoding.  Diagnostics and the LSP status are
   not drawn yet. */
static void draw_status(Editor *e, Grid *g, Win *w)
{
	static const char *const mode[] = { " NORMAL ", " VISUAL ", " V-LINE ", " V-BLOCK ", " INSERT ", " REPLACE ", " COMMAND " };
	static const unsigned char ma[] = { FACE_MODE_NORMAL, FACE_MODE_VISUAL, FACE_MODE_VISUAL, FACE_MODE_VISUAL,
	                                    FACE_MODE_INSERT, FACE_MODE_REPLACE, FACE_MODE_COMMAND },
	                           mb[] = { FACE_MODE_NORMAL_FG, FACE_MODE_VISUAL_FG, FACE_MODE_VISUAL_FG, FACE_MODE_VISUAL_FG,
	                                    FACE_MODE_INSERT_FG, FACE_MODE_REPLACE_FG, FACE_MODE_COMMAND_FG };
	static const char L[] = "\xee\x82\xb6", R[] = "\xee\x82\xb4";
	static Stl t;
	char s[64];
	int r = w->y + w->h, maxw = w->w, cur = w == e->win;
	Buf *b = w->buf;
	size_t ls = text_line_start(b->text, w->line), len = text_line_start(b->text, w->line + 1) - ls - 1, cut = 0, sep;
	/* another window's cursor may be past a line that changed: it is
	   moved to the line's end, as build_stl_str_hl() does */
	if (w->col > len) w->col = len;
	int m = !cur ? 0 : e->mode == MODE_CMDLINE ? 6 : e->mode == MODE_INSERT ? 4 + !!e->ks.replace
	      : e->ks.visual == 'v' ? 1 : e->ks.visual == 'V' ? 2 : e->ks.visual ? 3 : 0;
	unsigned char c = cur ? FACE_STATUS_LINE : FACE_STATUS_LINE_NC;
	t.n = 0;
	if (cur) {
		stl_add(&t, L, mb[m], 0);
		stl_add(&t, mode[m], ma[m], 0);
		stl_add(&t, R, mb[m], 0);
		cut = t.n;
	}
	/* branch: git's, with the mockup's icon (U+2387) */
	const char *br = cur ? git_branch(b) : NULL;
	if (br) {
		stl_add(&t, " \xe2\x8e\x87 ", c, 0);
		stl_add(&t, br, c, 0);
		stl_add(&t, " ", c, 0);
	}
	/* filename: expand('%:t') */
	const char *name = "[No Name]";
	if (b->path)
		for (const char *p = name = b->path; *p; p++)
#ifdef _WIN32
			if (*p == '/' || *p == '\\') name = p + 1;
#else
			if (*p == '/') name = p + 1;
#endif
	stl_add(&t, " ", c, 0);
	stl_add(&t, name, c, 0);
	stl_add(&t, core_modified(b) ? " \xe2\x97\x8f " : " ", c, 0);
	sep = t.n;
	/* 'fileencoding' is set by reading a file that exists, empty or not
	   (measured); encoding hides an empty one */
	const char *ft = cur && b->path ? syntax_filetype(b->path) : NULL;
	if (ft) {
		stl_add(&t, " ", c, 0);
		stl_add(&t, ft, c, 0);
		stl_add(&t, " ", c, 0);
	}
	/* the component separator goes between two that are drawn */
	if (cur && b->id.exists) stl_add(&t, ft ? "\xc2\xb7 utf-8 " : " utf-8 ", c, 0);
	if (cur) stl_add(&t, L, FACE_STATUS_INFO, FACE_STATUS_LINE);
	/* location: line('.') and charcol('.'), '%3d:%-2d' */
	size_t l = w->line + 1, ch = 1, lines = buf_lines(b);
	LineIt it;
	const unsigned char *u;
	lit_init(&it, b, w->line);
	for (size_t k, at = 0; at < w->col && (k = lit_next(&it, &u)); at += k) ch++;
	snprintf(s, sizeof s, " %3zu:%-2zu ", l, ch);
	stl_add(&t, s, cur ? mb[m] : c, cur ? FACE_STATUS_INFO : 0);
	if (cur) {
		stl_add(&t, L, mb[m], FACE_STATUS_INFO);
		/* progress: math.floor(cur / total * 100), in doubles as Lua's
		   (58 of 200 lines is 28%) */
		if (l == 1) stl_add(&t, " Top ", ma[m], 0);
		else if (l == lines) stl_add(&t, " Bot ", ma[m], 0);
		else {
			snprintf(s, sizeof s, " %2d%% ", (int)((double)l / (double)lines * 100));
			stl_add(&t, s, ma[m], 0);
		}
		stl_add(&t, R, mb[m], 0);
	}
	size_t n = t.n;
	char *out = t.o;
	unsigned char *f = t.f, *bl = t.l;
	u = (const unsigned char *)out;
	int width = grid_width(out, n), fill = 0;
	if (width > maxw) {
		if (width - grid_width(out + cut, n - cut) >= maxw) {
			/* what comes before %< does not fit: cut the end, '>' on the
			   character that reaches the width */
			size_t p = 0;
			for (int x = uni_width(u, n, p); x < maxw; x += uni_width(u, n, p)) p += uni_glen(u, n, p);
			out[p] = '>';
			n = p + 1;
		} else {
			/* drop characters from %< on and put '<' there, in the face of
			   the last one dropped */
			size_t k = 0;
			while (width >= maxw) {
				width -= uni_width(u, n, cut + k);
				k += uni_glen(u, n, cut + k);
			}
			unsigned char fc = f[cut + k - 1], lc = bl[cut + k - 1];
			memmove(out + cut + 1, out + cut + k, n - cut - k);
			memmove(f + cut + 1, f + cut + k, n - cut - k);
			memmove(bl + cut + 1, bl + cut + k, n - cut - k);
			out[cut] = '<';
			f[cut] = fc;
			bl[cut] = lc;
			n -= k - 1;
		}
	} else fill = maxw - width;
	/* the rest (half a double-width character) in the last face */
	for (int x = 0; x < maxw; x++) grid_put(g, r, w->x + x, (const unsigned char *)" ", 1, 1, ATTR_NONE);
	paint(g, r, w->x, maxw, f[n - 1]);
	/* the cell under a separator: the status line's fill when the status
	   line runs on to the right, else the separator (win_redr_status()) */
	if (w->frame->vsep) {
		int on = stl_connected(w);
		grid_put(g, r, w->x + w->w, (const unsigned char *)(on ? " " : "│"), on ? 1 : 3, 1, ATTR_NONE);
		paint(g, r, w->x + w->w, 1, !on ? FACE_WIN_SEPARATOR : c);
	}
	int x = 0;
	for (size_t i = 0; i < n && x < maxw; i += uni_glen(u, n, i)) {
		/* %=: the fill, in the face before it */
		if (i == sep && fill) {
			paint(g, r, w->x + x, fill, f[i - 1]);
			x += fill;
		}
		int cw = uni_width(u, n, i);
		if (x + cw <= maxw) {
			put(g, r, w->x + x, u + i, uni_glen(u, n, i), cw);
			paint(g, r, w->x + x, cw, f[i]);
			for (int k = 0; k < cw; k++) {
				Cell *z = wcell(g, w, w->h, x + k);
				if (z) z->bg = bl[i];
			}
		}
		x += cw;
	}
}

/* ---- the completion popup (popupmenu.c), one on the screen as there ---- */

enum { PUM_DEF_HEIGHT = 10, PUM_WIDTH = 15 };   /* 'pumheight' 0, 'pumwidth' 15 */
static struct {
	int row, col, height, width, base, kind, extra, scrollbar, first;
	const PumItem *items;
} pum;

static const char *pum_text(const PumItem *it, int j)
{
	const char *s = j == 0 ? it->word : j == 1 ? it->kind : it->menu;
	return s && *s ? s : NULL;
}

/* pum_display(): placement below the cursor if there is room, else above;
   then pum_set_selected() scrolls so the selected item has context */
void core_pum(Editor *e, const PumItem *items, int n, int sel, size_t col)
{
	if (items != pum.items || n <= 0) pum.first = 0;
	pum.items = items;
	e->pum = n > 0 ? items : NULL;
	e->npum = n > 0 ? n : 0;
	e->pumsel = sel;
	e->pumcol = col;
	if (!e->pum) return;
	Win *w = e->win;
	core_update(e);
	win_drawn(e);                      /* pum_call_update_screen() */
	/* validate_cursor_col() with the cursor at the word's start; the row
	   stays the cursor's */
	int off = win_numw(w), width = w->w - off;
	long c = (long)core_vcol(e, w->line, col) + off;
	if (w->wrap && c >= w->w && width > 0) c -= ((c - w->w) / width + 1) * width;
	c = c > (long)w->left ? c - (long)w->left : 0;
	int prow = w->y + w->frame->s.wrow, cursor_col = w->x + (int)c;
	int above_row = 0, below_row = e->layout->size[ROWS];
	if (below_row < w->y + w->h) below_row = w->y + w->h;
	/* pum_compute_vertical_placement() */
	pum.height = n < PUM_DEF_HEIGHT ? n : PUM_DEF_HEIGHT;
	if (prow + 2 >= below_row - pum.height && prow - above_row > (below_row - above_row) / 2) {
		int ctx = w->frame->s.wrow - w->frame->s.crow;
		if (ctx > 2) ctx = 2;
		if (prow >= n + ctx) { pum.row = prow - n - ctx; pum.height = n; }
		else { pum.row = 0; pum.height = prow - ctx; }
	} else {
		int ctx = w->frame->s.crow + w->frame->s.cheight - w->frame->s.wrow;
		if (ctx > 3) ctx = 3;
		pum.row = prow + ctx;
		pum.height = below_row - pum.row < n ? below_row - pum.row : n;
	}
	if (pum.height < 1 || (pum.height == 1 && n > 1)) { pum.height = 0; return; }
	/* pum_compute_size() */
	pum.base = pum.kind = pum.extra = 0;
	for (int i = 0; i < n; i++) {
		int *m[3] = { &pum.base, &pum.kind, &pum.extra };
		for (int j = 0; j < 3; j++) {
			const char *s = pum_text(&items[i], j);
			int cw = s ? grid_width(s, strlen(s)) + (j > 0) : 0;
			if (*m[j] < cw) *m[j] = cw;
		}
	}
	pum.scrollbar = pum.height < n;
	/* pum_compute_horizontal_placement() with set_pum_width_aligned_with_cursor() */
	int max_col = e->cols, desired = pum.base + pum.kind + pum.extra;
	int avail = max_col - cursor_col - pum.scrollbar;
	pum.col = cursor_col;
	pum.width = desired < PUM_WIDTH ? PUM_WIDTH : desired + 1;
	if (avail < pum.width) {
		if (avail > PUM_WIDTH) pum.width = avail;
		else if ((avail += cursor_col) > PUM_WIDTH) {
			pum.width = PUM_WIDTH + 1;
			pum.col = max_col - pum.width - pum.scrollbar;
		} else {
			pum.col = 0;
			pum.width = max_col - pum.scrollbar;
		}
	}
	/* pum_set_selected() */
	int h = pum.height, so = sel - h, context = h / 2;
	if (sel >= 0 && sel < n) {
		if (pum.first > sel - 4) {
			if (pum.first > sel - 2) {
				pum.first -= h - 2;
				if (pum.first < 0) pum.first = 0;
				else if (pum.first > sel) pum.first = sel;
			} else pum.first = sel;
		} else if (pum.first < so + 5) {
			if (pum.first < so + 3) pum.first = pum.first + h - 2 > so + 1 ? pum.first + h - 2 : so + 1;
			else pum.first = so + 1;
		}
		if (context > 3) context = 3;
		if (h > 2) {
			if (pum.first > sel - context) pum.first = sel - context > 0 ? sel - context : 0;
			else if (pum.first < sel + context - h + 1) pum.first = sel + context - h + 1;
		}
		if (pum.first > n - h) pum.first = n - h;
	}
}

/* pum_redraw(): word, kind and menu in columns, a space before the menu
   when it does not start at the screen's edge, '>' where text is cut, the
   scrollbar on the right */
static void draw_pum(Editor *e, Grid *g)
{
	if (!e->pum || !pum.height) return;
	int width = pum.width, col_off = pum.col > 0;
	int scroll_range = e->npum - pum.height, thumb_pos = 0, thumb_h = 1;
	if (pum.first > scroll_range) pum.first = scroll_range;
	if (pum.scrollbar) {
		thumb_h = pum.height * pum.height / e->npum;
		if (!thumb_h) thumb_h = 1;
		thumb_pos = (pum.first * (pum.height - thumb_h) + scroll_range / 2) / scroll_range;
	}
	int left = pum.col - col_off, iw[3] = { pum.base, pum.kind, pum.extra };
	for (int i = 0; i < pum.height; i++) {
		int idx = i + pum.first, r = pum.row + i, gc = col_off, tot = 0, trunc = 0;
		const PumItem *it = &e->pum[idx];
		/* the popup is a float: Neovim's compositor leaves the halves of a
		   double-width character it covers at its edges as they were,
		   where a write in the same grid would make them '>' and ' '
		   (measured: "ab漢 漢" under the popup's first cell, "全角" under
		   its last) */
		int end = left + col_off + width + pum.scrollbar;
		Cell *under = left > 0 && r < g->rows && left < g->cols && !g->c[r * g->cols + left].len
		              ? &g->c[r * g->cols + left - 1] : NULL, keep = under ? *under : (Cell){ 0 };
		Cell *after = r < g->rows && end < g->cols && !g->c[r * g->cols + end].len
		              ? &g->c[r * g->cols + end] : NULL, keep2 = after ? *after : (Cell){ 0 };
		for (int c = 0; c < col_off + width + pum.scrollbar; c++)
			grid_put(g, r, left + c, (const unsigned char *)" ", 1, 1, ATTR_NONE);
		for (int j = 0; j < 3; j++) {
			const char *p = pum_text(it, j);
			int next_empty = j == 2 || !pum_text(it, j + 1);
			if (p) {
				/* the text up to a tab, then two spaces for the tab */
				for (;;) {
					size_t n = strcspn(p, "\t");
					const unsigned char *s = (const unsigned char *)p;
					int cw = 0;
					for (size_t q = 0; q < n; q += uni_glen(s, n, q)) cw += uni_width(s, n, q);
					if (width - tot < cw + (next_empty ? 0 : 2)) trunc = 1;
					int avail = width - tot, shown = 0;
					for (size_t q = 0; q < n; q += uni_glen(s, n, q)) {
						int w1 = uni_width(s, n, q);
						if (trunc && shown + w1 > avail) break;
						put(g, r, left + gc + shown, s + q, uni_glen(s, n, q), w1);
						shown += w1;
					}
					gc += trunc ? shown : cw;
					if (p[n] != '\t') break;
					gc += 2;
					tot += 2;
					p += n + 1;
				}
			}
			int pad = j > 0 ? iw[1] + 1 : 1;
			if (j == 2 || (next_empty && (j == 1 || (j == 0 && !pum_text(it, 2)))) || pum.base + pad >= width)
				break;
			gc = col_off + pum.base + pad;
			tot = pum.base + pad;
		}
		if (trunc) grid_put(g, r, left + col_off + width - 1, (const unsigned char *)">", 1, 1, ATTR_NONE);
		paint(g, r, left, col_off + width, idx == e->pumsel ? FACE_PMENU_SEL : FACE_PMENU);
		if (pum.scrollbar)
			paint(g, r, left + col_off + width, 1, i >= thumb_pos && i < thumb_pos + thumb_h ? FACE_PMENU_THUMB : FACE_PMENU_SBAR);
		if (under) *under = keep;
		if (after) *after = keep2;
	}
}

void core_draw(Editor *e, Tab *t, Grid *g)
{
	(void)t;   /* the current one: no other tab page until package 4 adds them */
	layout_resize(e, g->rows, g->cols);
	core_update(e);
	win_drawn(e);
	grid_clear(g);
	for (Win *w = e->wins; w; w = w->next) {
		draw_win(e, g, w);
		if (w->frame->vsep)
			for (int r = 0; r < w->h; r++)
				if (grid_put(g, w->y + r, w->x + w->w, (const unsigned char *)"│", 3, 1, ATTR_NONE))
					paint(g, w->y + r, w->x + w->w, 1, FACE_WIN_SEPARATOR);
		draw_status(e, g, w);
	}
	/* the command line, or the mode and the last message (showmode) */
	int r = e->layout->size[ROWS];
	if (e->mode == MODE_CMDLINE) {
		char t[2] = { (char)e->cmdtype, 0 };
		int cc = grid_puts(g, r, 1, g->cols, e->cmd, e->cmdpos, ATTR_NONE, FACE_NONE);
		grid_puts(g, r, 0, 1, t, 1, ATTR_NONE, FACE_NONE);
		grid_puts(g, r, cc, g->cols, e->cmd + e->cmdpos, e->cmdlen - e->cmdpos, ATTR_NONE, FACE_NONE);
		g->crow = r;
		g->ccol = cc < g->cols ? cc : g->cols - 1;
		return;
	}
	/* the mode goes on the screen's last row (msg_pos_mode()), below a
	   taller command line; a message at the command line's first */
	static const char *const mode[] = { "-- VISUAL --", "-- VISUAL LINE --", "-- VISUAL BLOCK --", "-- INSERT --", "-- REPLACE --" };
	int m = e->mode == MODE_INSERT ? 3 + !!e->ks.replace : e->ks.visual == 'v' ? 0 : e->ks.visual == 'V' ? 1 : e->ks.visual ? 2 : -1;
	if (m >= 0) grid_puts(g, e->rows - 1, 0, g->cols, mode[m], strlen(mode[m]), ATTR_NONE, FACE_NONE);
	else grid_puts(g, r, 0, g->cols, e->msg, strlen(e->msg), ATTR_NONE, FACE_NONE);
	draw_pum(e, g);
	Win *w = e->win;
	g->crow = w->y + w->frame->s.wrow;
	g->ccol = w->x + w->frame->s.wcol;
}
