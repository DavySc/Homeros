/* include/paint.h: grids drawn on the CPU into 32-bit pixels with
   font.h's glyphs, as foot draws, for both window backends: Wayland's
   shm buffers (package 8, where this code started) and the Windows DIB
   (package 9), whose pixels are the same bytes (0xAARRGGBB in memory
   order B, G, R, A).  One painter, so the two systems draw alike and a
   fix lands once. */
#include <string.h>
#include "paint.h"
#include "font.h"
#include "image.h"

enum { ROW = 4096 };                /* the most bytes and glyphs one row shapes (4 bytes a cell) */
static int cw, ch, cb;              /* the font's cell and baseline, in pixels */
static int bw;                      /* pixels a row of the buffer being drawn */
static const uint32_t *back;        /* what shows where the base colour would, and its stride (paint.h) */
static int bs;

/* The window's defaults, Catppuccin Mocha like ref/theme.c's faces: base,
   text, and surface1 (catppuccin/nvim's LineNr) for ATTR_DIM.  Colours are
   opaque (alpha ff), so a buffer is ARGB8888 or XRGB8888 alike. */
static const uint32_t BG = 0xff000000 | PAINT_BASE, FG = 0xffcdd6f4, DIM = 0xff45475a;
static uint32_t rgb(const unsigned char c[3]) { return 0xff000000 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]; }

/* Callers keep rectangles inside the buffer: cells are, by the backends' size. */
static void fill(uint32_t *px, int x0, int y0, int w, int h, uint32_t c)
{
	if (w <= 0 || h <= 0) return;
	uint32_t *p = px + y0 * bw + x0;
	for (int i = 0; i < w; i++) p[i] = c;
	for (int y = 1; y < h; y++) memcpy(p + y * bw, p, (size_t)w * sizeof *p);   /* libc's copy is vectorized (build B) */
}

/* A rectangle of the background: c, or back's pixels where c is the base */
static void ground(uint32_t *px, int x0, int y0, int w, int h, uint32_t c)
{
	if (!back || c != BG) { fill(px, x0, y0, w, h, c); return; }
	for (int y = y0; y < y0 + h; y++) memcpy(px + y * bw + x0, back + y * bs + x0, (size_t)w * sizeof *px);
}

/* src premultiplied, of alpha a, over dst (alpha too) */
static uint32_t over(uint32_t dst, uint32_t src, unsigned a)
{
	uint32_t r = 0;
	for (int s = 0; s < 32; s += 8) r |= ((src >> s & 0xff) + ((dst >> s & 0xff) * (255 - a) + 127) / 255) << s;
	return r;
}

/* bg + (fg - bg) * a / 255 per channel, rounded, red and blue in one
   multiply, alpha and green in another (build B): each product fits its 16
   bits, and
   (x + 128 + ((x + 128) >> 8)) >> 8 is x / 255 rounded for x up to
   255 * 255 (Blinn, "Three wrongs make a right"); equal to dividing each
   channel by 255 for every bg, fg and a (proved: exhaustive, 33,554,432 cases) */
static uint32_t mix(uint32_t bg, uint32_t fg, unsigned a)
{
	uint32_t rb = (bg & 0xff00ff) * (255 - a) + (fg & 0xff00ff) * a + 0x800080;
	uint32_t ag = (bg >> 8 & 0xff00ff) * (255 - a) + (fg >> 8 & 0xff00ff) * a + 0x800080;
	rb = (rb + (rb >> 8 & 0xff00ff)) >> 8 & 0xff00ff;
	ag = (ag + (ag >> 8 & 0xff00ff)) >> 8 & 0xff00ff;
	return rb | ag << 8;
}

/* The colours of cell c of row r, as ref/tty.c: the attr, then the face's
   colours, reversed last (as under the cursor, cur); bold and italic need
   fonts font.h does not open */
static const Face *colours(const Grid *g, int r, int c, int cur, uint32_t *fg, uint32_t *bg, const Face **u)
{
	const Cell *x = &g->c[r * g->cols + c];
	const Face *b, *f = grid_face(x, &b, u);
	uint32_t t = *fg = f->style & FACE_FG ? rgb(f->fg) : x->attr == ATTR_DIM ? DIM : FG;
	*bg = b->style & FACE_BG ? rgb(b->bg) : BG;
	if (x->attr == ATTR_REV || cur) { *fg = *bg; *bg = t; }
	return f;
}

static int blank(const Cell *x) { return x->len == 1 && x->ch[0] == ' '; }

/* Draws cells lo to hi of row r of g into px: their backgrounds, then the
   cells shaped as one run, since colour does not change the font (kitty
   splits runs by font only).  A glyph wider than its cell (73 of Mathlib's
   295 symbols, reviews/package-7.md) spills into the blank cells after it,
   as foot's overflowing glyphs do, and a ligature's glyph ("->", JetBrains
   Mono's " /\ ") reaches back over its word, as in foot and kitty.  A glyph
   is cut at its word's start, which span never splits, so a partial redraw
   draws what a whole one does. */
static void draw_span(uint32_t *px, const Grid *g, int r, int lo, int hi)
{
	static unsigned char s[ROW];
	static unsigned short col[ROW];     /* the cell of each byte of s */
	static Placed pl[ROW];
	const Cell *x = &g->c[r * g->cols];
	int y0 = r * ch, ye = y0 + ch, n = 0;
	uint32_t fg, bg;
	if (lo) { s[n] = ' '; col[n++] = (unsigned short)(lo - 1); }   /* span starts after a blank: shape with it, as a whole row */
	for (int c = lo; c < hi; c++) {
		if (!x[c].len) continue;   /* right half: drawn with its left half */
		int w = c + 1 < g->cols && !x[c + 1].len ? 2 : 1;
		const Face *u, *f = colours(g, r, c, r == g->crow && c == g->ccol, &fg, &bg, &u);
		uint32_t sp = u->style & FACE_SP ? rgb(u->sp) : fg;
		ground(px, c * cw, y0, w * cw, ch, bg);
		if (x[c].attr == ATTR_IMAGE) { image_draw(px + y0 * bw + c * cw, bw, &x[c]); continue; }   /* thread 0's, agreed */
		if (u->style & FACE_UNDERCURL)
			for (int i = c * cw; i < (c + w) * cw; i++) fill(px, i, ye - 1 - grid_curl(i, ch), 1, 1, sp);
		else if (x[c].attr == PAINT_PRE || u->style & FACE_UNDERLINE) fill(px, c * cw, ye - 1, w * cw, 1, sp);
		if (f->style & FACE_STRIKE) fill(px, c * cw, y0 + ch / 2, w * cw, 1, fg);
		const unsigned char *t = grid_text(&x[c]);
		for (int i = 0; i < x[c].len && n < ROW; i++) { s[n] = t[i]; col[n++] = (unsigned short)c; }
	}
	for (int i = 0, m = font_shape(s, (size_t)n, pl, ROW); i < m; i++) {
		const Glyph *gl = pl[i].g;
		int c = col[pl[i].cluster], e = c + 1;
		while (e < hi && (!x[e].len || blank(&x[e]))) e++;
		int gx = c * cw + pl[i].x + gl->left, gy = y0 + cb - pl[i].y - gl->top, xe = e * cw;
		int w = c;
		while (w > 0 && !blank(&x[w - 1])) w--;
		int ax = gx < w * cw ? w * cw - gx : 0, zx = gx + gl->w > xe ? xe - gx : gl->w;
		int ay = gy < y0 ? y0 - gy : 0, zy = gy + gl->h > ye ? ye - gy : gl->h;
		/* the part over each earlier cell of its word in that cell's colours,
		   as kitty colours a ligature per cell, the rest in its own (a spill,
		   as foot's); each part reversed only where its cell is the cursor */
		for (int j0 = ax, j1; j0 < zx; j0 = j1) {
			int k = (gx + j0) / cw, m = (k + 1) * cw - gx;
			j1 = m < zx ? m : zx;
			k -= !x[k].len;   /* a right half: its left half's */
			const Face *u;
			colours(g, r, k < c ? k : c, r == g->crow && k == g->ccol, &fg, &bg, &u);
			for (int y = ay; y < zy; y++) {
				uint32_t *p = px + (gy + y) * bw + gx;
				if (gl->color) {   /* premultiplied BGRA, the buffer's own byte order */
					const unsigned char *q = gl->px + (size_t)(y * gl->w) * 4;
					for (int j = j0; j < j1; j++) {
						uint32_t v;
						memcpy(&v, q + 4 * j, 4);   /* the cache does not align it */
						unsigned a = v >> 24;
						if (a) p[j] = a == 255 ? v : over(p[j], v, a);
					}
					continue;
				}
				const unsigned char *a = gl->px + y * gl->w;
				for (int j = j0; j < j1; j++)
					if (a[j]) p[j] = a[j] == 255 ? fg : mix(p[j], fg, a[j]);
			}
		}
	}
}

/* Whether cell c of row r of a shows what cell c of row q of b shows */
static int cell_eq(const Grid *a, int r, const Grid *b, int q, int c)
{
	return (a->crow == r && a->ccol == c) == (b->crow == q && b->ccol == c)
		&& grid_cell_eq(&a->c[r * a->cols + c], &b->c[q * b->cols + c]);
}

/* The cells of row r of g that differ from row q of b, as *lo to *hi (only
   these are drawn, as Neovim's grid_put_linebuf sends only them), widened
   on each side to the start of a word after a blank in both rows: no spill
   comes in from the left, and the span's own stop at its right edge.  How
   many cells, 0 if none differ. */
int paint_span(const Grid *g, int r, const Grid *b, int q, int *lo, int *hi)
{
	int n = g->cols, l = 0, h = n;
	const Cell *x = &g->c[r * n], *y = &b->c[q * n];
	if (g->crow != r && b->crow != q && !memcmp(x, y, sizeof *x * (size_t)n)) return *lo = *hi = 0;   /* the usual row: one compare */
	while (l < n && cell_eq(g, r, b, q, l)) l++;
	if (l == n) return *lo = *hi = 0;
	while (h > l && cell_eq(g, r, b, q, h - 1)) h--;
#define EDGE(c) (blank(&x[(c) - 1]) && blank(&y[(c) - 1]) && !blank(&x[c]) && !blank(&y[c]))
	while (l > 0 && !EDGE(l)) l--;
	while (h < n && !EDGE(h)) h++;
#undef EDGE
	*lo = l;
	*hi = h;
	return h - l;
}

/* A hash of row r's text (FNV-1a over what grid_cell_eq compares), without
   the number column (ATTR_DIM), which relative numbers change on a scroll,
   and the background layer, which the cursor line keeps on its row */
static unsigned long row_hash(const Grid *g, int r)
{
	unsigned long h = 2166136261u;
	for (const Cell *x = &g->c[r * g->cols], *e = x + g->cols; x < e; x++) {
		if (x->attr == ATTR_DIM) continue;
		h = (h ^ x->len) * 16777619u;
		h = (h ^ x->attr) * 16777619u;
		h = (h ^ x->face) * 16777619u;
		for (int i = 0; i < x->len && i < 4; i++) h = (h ^ x->ch[i]) * 16777619u;   /* a long cluster: its number, as grid_cell_eq */
	}
	return h;
}

/* How far the rows moved since b was drawn: the move that most changed rows
   of g have, among b's rows, as Emacs's redisplay finds a scroll by row
   hashes (dispnew.c, scrolling_window); 0 if none.  The core sends whole
   grids, not scroll events.  A hash only picks the move: what is drawn
   comes from span. */
static int shift(const Grid *g, const Grid *b)
{
	int n = g->rows, best = 0, most = 0, changed = 0;
	/* a move needs two changed rows; typing changes one, and hashing both
	   grids cost a keystroke's frame 35 of its 55 us (src/win/probe/frames.c
	   under Wine, 3 runs).  memcmp may call equal rows changed (a long
	   cluster's store index), which only costs the hashing again */
	for (int r = 0; r < n && changed < 2; r++)
		changed += memcmp(&g->c[r * g->cols], &b->c[r * g->cols], sizeof *g->c * (size_t)g->cols) != 0;
	if (changed < 2) return 0;
	unsigned long hg[n], hb[n];
	for (int r = 0; r < n; r++) { hg[r] = row_hash(g, r); hb[r] = row_hash(b, r); }
	for (int d = 1; d < n; d++)
		for (int k = d; k >= -d; k -= 2 * d) {
			int m = 0;
			for (int r = k < 0 ? -k : 0; r < n && r + k < n; r++) m += hg[r] != hb[r] && hg[r] == hb[r + k];
			if (m > most) { most = m; best = k; }
		}
	return best;
}

void paint_clear(uint32_t *px, int stride, const uint32_t *bk, int bstride, int w, int h)
{
	bw = stride;
	back = bk;
	bs = bstride;
	ground(px, 0, 0, w, h, BG);
}

void paint(uint32_t *px, int stride, const uint32_t *bk, int bstride, const Grid *g, Grid *b, int all, unsigned char *dirty)
{
	font_cell(&cw, &ch, &cb);
	bw = stride;
	back = bk;
	bs = bstride;
	/* rows a scroll moved are copied, not shaped again (shaping all 60 rows
	   takes p99 1.91 ms of the 2 ms frame, reviews/package-7.md), and of a
	   row only the cells that changed are drawn.  Copying in the direction
	   of the move reads each source before it is drawn over.  Not over a
	   wallpaper that differs by row, which would move with the text. */
	int k = all || (back && bs) ? 0 : shift(g, b);
	size_t row = (size_t)ch * (size_t)bw;
	for (int i = 0; i < g->rows; i++) {
		int r = k < 0 ? g->rows - 1 - i : i, lo = 0, hi = g->cols, l, h;
		dirty[r] = 1;
		if (!all) {
			int n = paint_span(g, r, b, r, &lo, &hi);
			if (!(dirty[r] = n > 0)) continue;
			if (k && r + k >= 0 && r + k < g->rows && paint_span(g, r, b, r + k, &l, &h) < n) {
				memcpy(px + (size_t)r * row, px + (size_t)(r + k) * row, row * sizeof *px);
				lo = l;
				hi = h;
			}
		}
		if (lo < hi) draw_span(px, g, r, lo, hi);
		memcpy(&b->c[r * g->cols], &g->c[r * g->cols], sizeof *g->c * (size_t)g->cols);
	}
	b->crow = g->crow;
	b->ccol = g->ccol;
}
