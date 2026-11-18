/* Drawing the screen: windows (drawscreen.c win_update(), drawline.c
   win_line()), status lines (statusline.c build_stl_str_hl() with the
   default 'statusline'), the command line and the completion popup
   (popupmenu.c).  Allocates nothing. */
#include <stdio.h>
#include <string.h>
#include "core_int.h"
#include "syntax.h"
#include "uni.h"

/* A cluster into the grid.  One that starts with a character that would
   join a space is drawn on a space (utf_iscomposing_first(),
   schar_from_buf_first()); the grid cuts what is then too long. */
static void put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr)
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
	grid_put(g, r, c, s, n, width, attr);
}

/* The text area of window w: a cell put at window row r, column c, clipped
   to the window. */
static void wput(Grid *g, const Win *w, int r, int c, const unsigned char *s, size_t n, int width, int attr)
{
	if (c >= 0 && c + width <= w->w) put(g, w->y + r, w->x + c, s, n, width, attr);
}

static void wputs(Grid *g, const Win *w, int r, int c, const char *s, int attr)
{
	for (; *s; s++, c++) wput(g, w, r, c, (const unsigned char *)s, 1, 1, attr);
}

/* Cell j of the character s (k bytes) that takes width cells: a tab is
   spaces, a control character ^X, an invalid byte or what cannot be
   printed in hex (transchar_hex()). */
static void put_cell(Grid *g, const Win *w, int r, int c, const unsigned char *s, size_t k, int j,
                     int width, unsigned char face, unsigned char bg)
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
	else { b = 0; wput(g, w, r, c, s, k, width, ATTR_NONE); }
	if (b) wput(g, w, r, c, &b, 1, 1, ATTR_NONE);
	/* a window can reach past a terminal that shrank (see grid_put) */
	int y = w->y + r, x = w->x + c;
	if (c >= 0 && c < w->w && y >= 0 && y < g->rows && x >= 0 && x < g->cols) {
		g->c[y * g->cols + x].face = face;
		g->c[y * g->cols + x].bg = bg;
	}
}

/* The number column of line l (get_line_number_str()): relative to the
   cursor line, which shows its own number left-aligned.  Returns the
   column the "<<<" marker reaches (0: none), which the text leaves. */
static int put_number(Grid *g, const Win *w, int r, size_t l, int top_skipped)
{
	char b[40];
	int nw = win_numw(w) - 1;
	size_t rel = l > w->line ? l - w->line : w->line - l;
	snprintf(b, sizeof b, rel ? "%*zu " : "%-*zu ", nw, rel ? rel : l + 1);
	wputs(g, w, r, 0, b, ATTR_DIM);
	if (!top_skipped) return 0;
	/* skipcol hides the line's start: "  5" becomes "--5" and three '<'
	   follow the digits, over the text's first cells when the digits fill
	   the column (win_line(), draw_lnum_col(); measured on lines 9, 10, 100) */
	int d = 0;
	for (; b[d] == ' '; d++) wputs(g, w, r, d, "-", ATTR_DIM);
	while (b[d] >= '0' && b[d] <= '9') d++;
	wputs(g, w, r, d, "<<<", ATTR_DIM);
	return d + 3;
}

/* win_line()'s Visual area of line l in w ('selection' inclusive): cells
   [*from, *to), none if *from >= *to, less the cursor's [*xf, *xt], which
   the block cursor shows (noinvcur: its cell, both of a double-width
   character's).  Windows on the current buffer show it too. */
static void visual(Editor *e, Win *w, size_t l, size_t *from, size_t *to, size_t *xf, size_t *xt)
{
	const Win *cw = e->win;
	size_t cl = cw->line, vl = e->ks.vline, a, b, c, d, n;
	*from = *xf = 1;
	*to = *xt = 0;
	if (!e->ks.visual || w->buf != e->buf) return;
	/* ltoreq(): the cursor is the top end unless VIsual is before it */
	int vtop = vl < cl || (vl == cl && e->ks.vcol < cw->col);
	size_t tl = vtop ? vl : cl, tc = vtop ? e->ks.vcol : cw->col, bl = vtop ? cl : vl, bc = vtop ? cw->col : e->ks.vcol;
	if (l < tl || l > bl) return;
	if (e->ks.visual == 22) {
		/* block: w_old_cursor_fcol and _lcol (getvcols()), to the line's
		   end after $ */
		win_vcols(w, cl, cw->col, &a, &b);
		win_vcols(w, vl, e->ks.vcol, &c, &d);
		*from = a < c ? a : c;
		*to = cw->want == WANT_EOL ? (size_t)-1 : (b > d ? b : d) + 1;
	} else {
		*from = 0;
		*to = (size_t)-1;
		if (l == tl && e->ks.visual == 'v') {
			win_vcols(w, tl, tc, from, &b);
			core_line(e, tl, &n);
			if (tc >= n) *to = *from + 1;
		}
		if (l == bl && e->ks.visual == 'v') {
			win_vcols(w, bl, bc, &a, &b);
			*to = b + 1;
		}
	}
	if (w == cw && l == cl) {
		*xf = win_vcol(e, w, NULL, xt);
		const unsigned char *t = core_line(e, cl, &n);
		if (cw->col >= n || t[cw->col] < 0x80 || uni_width(t, n, cw->col) < 2) *xt = *xf;
	}
}

/* Line l from window row r: in rows of the text width with wrap (the top
   line from skipcol), else one row from leftcol.  Returns 1 if the line was
   drawn to its end. */
static int draw_line(Editor *e, Grid *g, Win *w, size_t l, int r)
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
	size_t from, to, xf, xt;
	visual(e, w, l, &from, &to, &xf, &xt);
	lit_init(&it, w->buf, l);
	while ((k = lit_next(&it, &s))) {
		int cw = core_cellw(w, s, k, 0, v);
		if (syn && (f0 == (size_t)-1 || i + k > f0 + sizeof fc)) {
			size_t z = text_line_start(w->buf->text, l + 1) - 1;
			f0 = i;
			syntax_faces(syn, ls + f0, ls + f0 + sizeof fc < z ? ls + f0 + sizeof fc : z, fc);
		}
		/* a double-width character: after the '>' of a wrapping row's
		   last cell (core_cellw's 3), the two cells of the character */
		int lead = cw == 3 && *s != '\t', wide = *s >= 0x80 && cw - lead == 2;
		for (int j = 0; j < cw; j++, v++) {
			if (v < skip) continue;
			size_t d = v - skip;
			int row = w->wrap ? r + (int)(d / (size_t)tw) : r;
			if (!w->wrap && d >= (size_t)tw) return 1;
			if (row >= w->h) return 0;
			int c = nw + (int)(d % (size_t)tw);
			/* a double-width character whose second cell starts the area
			   is in it (win_line()) */
			unsigned char face = syn ? fc[i - f0] : 0;
			unsigned char bg = (v >= from || (wide && v + 1 == from)) && v < to && (v < xf || v > xt) ? FACE_VISUAL : 0;
			if (row == r && c < cover) {
				/* "<<<" over a double-width character's first cell leaves a
				   blank in its second (measured) */
				if (wide && j == lead && c + 1 == cover) put_cell(g, w, row, c + 1, (const unsigned char *)" ", 1, 0, 1, face, bg);
				continue;
			}
			/* win_line(): '>' where a double-width character does not fit
			   (the line's end without wrap), '<' where the view starts in
			   its second half (MB_FILLER_CHAR) */
			if (j < lead || (wide && j == 0 && !w->wrap && d == (size_t)tw - 1))
				put_cell(g, w, row, c, (const unsigned char *)">", 1, 0, 1, face, 0);   /* not in the area (measured) */
			else if (wide && j - lead == 1 && v == skip)   /* in the area if its character is (measured) */
				put_cell(g, w, row, c, (const unsigned char *)"<", 1, 0, 1, face, from < v ? bg : 0);
			else put_cell(g, w, row, c, s, k, j - lead, cw - lead, face, bg);
		}
		i += k;
	}
	/* win_line(): "Invert at least one char" where the area starts at the
	   line's end (a blank cell; in block mode only on the lines of its two
	   ends), or the last cell when the end is past the window's edge.  A
	   line that ends before the view starts has its end at the view's
	   start, where an area that starts earlier starts too. */
	size_t vs = v > skip ? v : skip;
	if ((v > skip ? from == v : from <= skip) && vs < to && (vs < xf || vs > xt)
	    && (e->ks.visual != 22 || l == e->win->line || l == e->ks.vline)) {
		size_t d = vs - skip;
		int full = w->wrap ? d && d % (size_t)tw == 0 : d >= (size_t)tw;
		d -= (size_t)full;
		int row = w->wrap ? r + (int)(d / (size_t)tw) : r, c = nw + (int)(d % (size_t)tw), y = w->y + row, x = w->x + c;
		if (!full) wput(g, w, row, c, (const unsigned char *)" ", 1, 1, ATTR_NONE);
		if (row < w->h && y < g->rows && x < g->cols) g->c[y * g->cols + x].bg = FACE_VISUAL;
	}
	return 1;
}

static int line_rows(const Win *w, size_t l)
{
	int tw = w->w - win_numw(w);
	if (!w->wrap || tw <= 0) return 1;
	size_t v = win_vcol_end(w, l), skip = l == w->top ? w->skipcol : 0;
	return v <= skip ? 1 : (int)((v - skip + (size_t)tw - 1) / (size_t)tw);
}

/* win_update(): the lines from the top, "@@@" where the last one does not
   fit (display=lastline), "~" below the end of the buffer */
static void draw_win(Editor *e, Grid *g, Win *w)
{
	size_t n = buf_lines(w->buf), l = w->top;
	int r = 0;
	for (; r < w->h && l < n; l++) {
		int rows = line_rows(w, l);
		if (!draw_line(e, g, w, l, r)) {
			/* a double-width character the "@@@" starts in becomes '@' too
			   (measured: four '@' after one) */
			int c0 = w->w - 3 < 0 ? 0 : w->w - 3, y = w->y + w->h - 1, x = w->x + c0;
			if (c0 > 0 && y < g->rows && x < g->cols && !g->c[y * g->cols + x].len) c0--;
			if (l != w->top)
				for (int c = c0; c < w->w; c++)
					wput(g, w, w->h - 1, c, (const unsigned char *)"@", 1, 1, ATTR_DIM);
			return;
		}
		r += rows;
	}
	for (; r < w->h; r++) wputs(g, w, r, 0, "~", ATTR_DIM);
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

static int cells(const char *s)
{
	int c = 0;
	size_t n = strlen(s);
	for (size_t i = 0; i < n; i += uni_glen((const unsigned char *)s, n, i))
		c += uni_width((const unsigned char *)s, n, i);
	return c;
}

/* The default 'statusline', "%<%f %h%w%m%r %=%-14.(%l,%c%V%) %P", as
   build_stl_str_hl() lays it out in the window's width. */
static void draw_status(Editor *e, Grid *g, Win *w)
{
	char out[600], *right;
	int r = w->y + w->h, maxw = w->w;
	size_t len = text_line_start(w->buf->text, w->line + 1) - text_line_start(w->buf->text, w->line) - 1;
	/* another window's cursor may be past a line that changed: it is
	   moved to the line's end, as build_stl_str_hl() does */
	if (w->col > len) w->col = len;
	/* %c is 0 on an empty line outside insert mode; %V shows the screen
	   column when it differs */
	size_t col = e->mode != MODE_INSERT && !len ? 0 : w->col + 1, vcol = win_vcol(e, w, NULL, NULL) + 1;
	char grp[64], pos[8];
	/* %l is 0 in an empty buffer */
	int k = snprintf(grp, sizeof grp, "%zu,%zu", w->buf->flags & CORE_EMPTY ? 0 : w->line + 1, col);
	if (vcol != col) snprintf(grp + k, sizeof grp - (size_t)k, "-%zu", vcol);
	/* %P: get_rel_pos() */
	size_t above = w->top, below = buf_lines(w->buf) - win_botline(e, w, NULL);
	if (!below) snprintf(pos, sizeof pos, above ? "Bot" : "All");
	else if (!above) snprintf(pos, sizeof pos, "Top");
	else snprintf(pos, sizeof pos, "%2d%%", (int)(above > 1000000 ? above / ((above + below) / 100)
	                                                               : above * 100 / (above + below)));
	k = snprintf(out, sizeof out, "%s %s ", w->buf->path ? w->buf->path : "[No Name]",
	             core_modified(w->buf) ? "[+]" : "");
	if (k >= (int)sizeof out - 80) k = (int)sizeof out - 80;
	right = out + k;
	snprintf(right, 80, "%-14s %s", grp, pos);
	for (int c = 0; c <= w->w; c++) grid_put(g, r, w->x + c, (const unsigned char *)" ", 1, 1, ATTR_REV);
	if (w->frame->vsep && !stl_connected(w))
		grid_put(g, r, w->x + w->w, (const unsigned char *)"│", 3, 1, ATTR_REV);
	int width = cells(out), c = 0, fill = width < maxw;
	const unsigned char *s = (const unsigned char *)out;
	size_t n = strlen(out), i = 0;
	if (width > maxw) {
		/* too long: drop characters from the start (the %< item) and put '<' */
		while (width >= maxw && i < n) {
			width -= uni_width(s, n, i);
			i += uni_glen(s, n, i);
		}
		if (maxw > 0) grid_put(g, r, w->x, (const unsigned char *)"<", 1, 1, ATTR_REV);
		c = 1;
	}
	for (; i < n && c < maxw; i += uni_glen(s, n, i)) {
		/* %=: the fill goes where the right part starts */
		if (s + i == (const unsigned char *)right && fill) c += maxw - width;
		int cw = uni_width(s, n, i);
		if (c + cw <= maxw) put(g, r, w->x + c, s + i, uni_glen(s, n, i), cw, ATTR_REV);
		c += cw;
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
			int cw = s ? cells(s) + (j > 0) : 0;
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
		int attr = idx == e->pumsel ? ATTR_NONE : ATTR_REV;
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
			grid_put(g, r, left + c, (const unsigned char *)" ", 1, 1, attr);
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
						put(g, r, left + gc + shown, s + q, uni_glen(s, n, q), w1, attr);
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
		if (trunc) grid_put(g, r, left + col_off + width - 1, (const unsigned char *)">", 1, 1, attr);
		if (pum.scrollbar) {
			int thumb = i >= thumb_pos && i < thumb_pos + thumb_h;
			grid_put(g, r, left + col_off + width, (const unsigned char *)" ", 1, 1, thumb ? ATTR_NONE : ATTR_DIM);
		}
		if (under) *under = keep;
		if (after) *after = keep2;
	}
}

void core_draw(Editor *e, Grid *g)
{
	layout_resize(e, g->rows, g->cols);
	core_update(e);
	win_drawn(e);
	grid_clear(g);
	for (Win *w = e->wins; w; w = w->next) {
		draw_win(e, g, w);
		if (w->frame->vsep)
			for (int r = 0; r < w->h; r++)
				grid_put(g, w->y + r, w->x + w->w, (const unsigned char *)"│", 3, 1, ATTR_REV);
		draw_status(e, g, w);
	}
	/* the command line, or the mode and the last message (showmode) */
	int r = e->layout->size[ROWS];
	if (e->mode == MODE_CMDLINE) {
		char t[2] = { (char)e->cmdtype, 0 };
		int c = grid_puts(g, r, 0, t, ATTR_NONE), cc = c;
		const unsigned char *s = (const unsigned char *)e->cmd;
		for (size_t i = 0; i < e->cmdlen; i += uni_glen(s, e->cmdlen, i)) {
			if (i == e->cmdpos) cc = c;
			c += grid_put(g, r, c, s + i, uni_glen(s, e->cmdlen, i), uni_width(s, e->cmdlen, i), ATTR_NONE);
		}
		if (e->cmdpos >= e->cmdlen) cc = c;
		g->crow = r;
		g->ccol = cc < g->cols ? cc : g->cols - 1;
		return;
	}
	/* the mode goes on the screen's last row (msg_pos_mode()), below a
	   taller command line; a message at the command line's first */
	static const char *const mode[] = { "-- VISUAL --", "-- VISUAL LINE --", "-- VISUAL BLOCK --", "-- INSERT --", "-- REPLACE --" };
	int m = e->mode == MODE_INSERT ? 3 + !!e->ks.replace : e->ks.visual == 'v' ? 0 : e->ks.visual == 'V' ? 1 : e->ks.visual ? 2 : -1;
	if (m >= 0) grid_puts(g, e->rows - 1, 0, mode[m], ATTR_NONE);
	else grid_puts(g, r, 0, e->msg, ATTR_NONE);
	draw_pum(e, g);
	Win *w = e->win;
	g->crow = w->y + w->frame->s.wrow;
	g->ccol = w->x + w->frame->s.wcol;
}
