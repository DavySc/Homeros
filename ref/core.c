/* Reference core: one buffer in one window, no wrapping, no popup; the
   calls for more buffers and windows refuse. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "syntax.h"
#include "uni.h"

enum { SCROLLOFF = 6, TABSTOP = 8 };   /* FEATURES.md 1; Vim's tabstop */

/* every change reaches the syntax tree before it is made (UndoHook) */
static void undo_seen(void *ctx, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	Buf *b = ctx;
	if (b->syn) syntax_edit(b->syn, t, pos, ndel, ins, nins);
}

int core_open(Editor *e, const char *path)
{
	char *data = NULL;
	size_t n = 0;
	memset(e, 0, sizeof *e);
	e->rows = 24; e->cols = 80;
	e->buf = e->bufs = calloc(1, sizeof *e->buf);
	e->win = e->wins = calloc(1, sizeof *e->win);
	if (!e->buf || !e->win || !(e->buf->path = strdup(path))) return -1;
	e->buf->nr = 1;
	e->win->buf = e->buf;
	e->win->h = e->rows - 2;
	if (file_load(path, &data, &n, &e->buf->id)) {
		core_msg(e, "\"%s\": cannot read", path);
		return -1;
	}
	e->buf->noeol = n == 0 || data[n - 1] != '\n';
	e->buf->flags = n ? 0 : CORE_EMPTY;
	e->buf->text = text_new(data, n);
	free(data);
	e->buf->undo = undo_new();
	if (!e->buf->text || !e->buf->undo || (e->buf->noeol && text_insert(e->buf->text, n, "\n", 1))) {
		core_msg(e, "out of memory");
		return -1;
	}
	e->buf->syn = syntax_open(path, e->buf->text);
	undo_hook(e->buf->undo, undo_seen, e->buf);
	core_set_want(e);
	core_msg(e, "\"%s\"%s %zuL, %zuB", path, e->buf->id.exists ? "" : " [New]", core_lines(e), n);
	return 0;
}

void core_free(Editor *e)
{
	if (!e->buf || !e->win) { free(e->buf); free(e->win); return; }
	syntax_free(e->buf->syn);
	text_free(e->buf->text);
	undo_free(e->buf->undo);
	free(e->buf->path);
	free(e->buf);
	free(e->win);
	free(e->lb);
}

size_t core_lines(const Editor *e) { return text_lines(e->buf->text); }
size_t core_line_start(const Editor *e, size_t l) { return text_line_start(e->buf->text, l); }

size_t core_line_len(const Editor *e, size_t l)
{
	return text_line_start(e->buf->text, l + 1) - text_line_start(e->buf->text, l) - 1;
}

const unsigned char *core_line(Editor *e, size_t l, size_t *len)
{
	size_t n = core_line_len(e, l);
	if (n + 1 > e->lbcap) {
		unsigned char *b = realloc(e->lb, n + 1);
		if (!b) { *len = 0; return (const unsigned char *)""; }
		e->lb = b; e->lbcap = n + 1;
	}
	*len = text_read(e->buf->text, core_line_start(e, l), (char *)e->lb, n);
	return e->lb;
}

static int edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins, unsigned after)
{
	char *del = malloc(ndel ? ndel : 1);
	UndoCursor c = { e->win->line, e->win->col, e->buf->flags };
	int r = -1;
	/* the syntax tree hears of the change first; the thin editor does not
	   tell it of the rollback below (out of memory), package 4 must */
	if (del && text_read(e->buf->text, pos, del, ndel) == ndel) undo_seen(e->buf, e->buf->text, pos, ndel, ins, nins);
	else { free(del); del = NULL; }
	if (del && !text_delete(e->buf->text, pos, ndel)) {
		if (!text_insert(e->buf->text, pos, ins, nins)) {
			if (!undo_record(e->buf->undo, pos, del, ndel, ins, nins, c, after)) r = 0;
			else { text_delete(e->buf->text, pos, nins); text_insert(e->buf->text, pos, del, ndel); }
		} else text_insert(e->buf->text, pos, del, ndel);
	}
	free(del);
	e->win->vc.ok = 0;
	if (r) core_msg(e, "out of memory");
	/* an empty file saves empty until it gets a line of text (Vim) */
	else {
		e->buf->flags = after;
		if (e->buf->noeol && !e->buf->id.size) e->buf->noeol = 0;
	}
	return r;
}

int core_edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	return edit(e, pos, ndel, ins, nins, 0);
}

int core_delete_lines(Editor *e, size_t a, size_t z)
{
	size_t n = core_lines(e);
	if (a == 0 && z >= n) return edit(e, 0, core_line_start(e, n) - 1, "", 0, CORE_EMPTY);
	return edit(e, core_line_start(e, a), core_line_start(e, z) - core_line_start(e, a), "", 0, 0);
}

/* The thin editor does not wrap. */
void core_set_wrap(Editor *e, int on) { (void)e; (void)on; }

/* The thin editor keeps marks but does not move them (package 4 does). */
void core_mark_join(Editor *e, size_t line, size_t mincol, long line_amount, long col_amount, int spaces_removed)
{
	(void)e; (void)line; (void)mincol; (void)line_amount; (void)col_amount; (void)spaces_removed;
}

/* Neovim's cursor after undo and redo (undo.c, u_undoredo): back where the
   change started if that is inside or next to the changed lines, else the
   first changed line at its first non-blank. */
int core_undo(Editor *e, int redo)
{
	UndoWhere w;
	size_t before = core_lines(e), c = e->win->line;
	int r = redo ? undo_redo(e->buf->undo, e->buf->text, &w) : undo_undo(e->buf->undo, e->buf->text, &w);
	if (r <= 0) {
		core_msg(e, r ? "out of memory" : redo ? "Already at newest change" : "Already at oldest change");
		return -1;
	}
	size_t n = core_lines(e);
	size_t first = text_line_of(e->buf->text, w.lo), last;
	e->buf->flags = w.flags;
	int at_start = w.lo == core_line_start(e, first);
	if (w.hi == w.lo && at_start) last = first - 1;   /* whole lines removed: none left */
	else last = text_line_of(e->buf->text, w.hi > w.lo ? w.hi - 1 : w.lo);
	size_t l = w.cur.line + 1 >= first && w.cur.line <= last + 1 ? w.cur.line : first;
	if (l >= n) l = n - 1;                          /* check_cursor_lnum() */
	if (w.cur.line + 1 == l && l > 0) l--;          /* "o": back where it started */
	e->win->line = l;
	/* else beginline(BL_SOL | BL_FIX), which with 'nostartofline' is
	   coladvance(curswant): the column wanted before the command, since
	   curswant is recomputed only once the whole count is done */
	e->win->col = w.cur.line == l ? w.cur.col : core_col_at(e, l, e->win->want, 0);
	core_clamp(e);
	core_msg(e, "");
	/* u_undoredo() deletes the old lines, check_cursor_lnum(), inserts the
	   new ones, then changed_lines(): that invalidates the cached cursor
	   column only if the clamped cursor is at or below the first changed
	   line, which fails when the old lines ran to the end of the buffer */
	size_t kept = before - (n - (last + 1 - first));   /* lines the step replaced */
	if (c >= before - kept) c = before - kept ? before - kept - 1 : 0;
	if (c >= first) e->win->vc.ok = 0;
	return c >= first;
}

int core_modified(const Buf *b) { return undo_seq(b->undo) != b->saved_seq; }

int core_write(Editor *e, Buf *b, int force)
{
	char kept[4096];
	/* an empty buffer (ML_EMPTY) writes no bytes, not one empty line */
	size_t n = b->flags & CORE_EMPTY ? 0 : text_len(b->text) - (b->noeol ? 1 : 0);
	switch (file_save(b->path, b->text, n, &b->id, force, kept, sizeof kept)) {
	case SAVE_OK:
		b->saved_seq = undo_seq(b->undo);
		core_msg(e, "\"%s\" %zuL, %zuB written", b->path, text_lines(b->text), n);
		return 0;
	case SAVE_CHANGED:
		core_msg(e, "E13: \"%s\" changed on disk since it was read (add ! to override)", b->path);
		return -1;
	case SAVE_KEPT:
		b->saved_seq = undo_seq(b->undo);
		core_msg(e, "written, but another program replaced the file first; its version is in %s", kept);
		return 0;
	default:
		core_msg(e, "E212: Can't open file for writing: %s", b->path);
		return -1;
	}
}

void core_msg(Editor *e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e->msg, sizeof e->msg, fmt, ap);
	va_end(ap);
}

static int cellw(const unsigned char *s, size_t n, size_t i, size_t v)
{
	unsigned c = uni_cp(s, n, i);
	if (c == '\t') return TABSTOP - (int)(v % TABSTOP);
	if (c < 0x20 || c == 0x7f) return 2;                      /* ^X */
	return uni_width(c);
}

size_t core_vcol(Editor *e, size_t l, size_t col)
{
	size_t n, v = 0;
	const unsigned char *s = core_line(e, l, &n);
	for (size_t i = 0; i < col && i < n; i += uni_len(s, n, i)) v += (size_t)cellw(s, n, i, v);
	return v;
}

size_t core_col_at(Editor *e, size_t l, size_t want, int past_end)
{
	size_t n, v = 0, i = 0, last = 0;
	const unsigned char *s = core_line(e, l, &n);
	while (i < n) {
		size_t w = (size_t)cellw(s, n, i, v);
		if (want != WANT_EOL && v + w > want) return i;
		v += w;
		last = i;
		i += uni_len(s, n, i);
	}
	return past_end ? n : last;
}

size_t core_first_nonblank(Editor *e, size_t l)
{
	size_t n, i = 0;
	const unsigned char *s = core_line(e, l, &n);
	while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
	if (i == n && i > 0 && e->mode != MODE_INSERT) i = uni_prev(s, n, n);
	return i;
}

void core_clamp(Editor *e)
{
	size_t n = core_lines(e), len;
	if (e->win->line >= n) e->win->line = n - 1;
	const unsigned char *s = core_line(e, e->win->line, &len);
	if (e->mode == MODE_INSERT) { if (e->win->col > len) e->win->col = len; return; }
	if (e->win->col >= len) e->win->col = len ? uni_prev(s, len, len) : 0;
	else {
		/* a column restored by undo may fall inside a character: back to
		   its first byte, as check_cursor_col() does with mb_adjust_cursor() */
		size_t p = 0;
		for (size_t k; p + (k = uni_len(s, len, p)) <= e->win->col; p += k) {}
		e->win->col = p;
	}
}

void core_set_want(Editor *e)
{
	Win *w = e->win;
	if (w->vc.ok && w->vc.line == w->line && w->vc.col == w->col && w->vc.mode == e->mode) {
		w->want = w->vc.v;
		return;
	}
	size_t n;
	const unsigned char *s = core_line(e, w->line, &n);
	w->want = core_vcol(e, w->line, w->col);
	if (e->mode == MODE_NORMAL && w->col < n && s[w->col] == '\t')
		w->want = core_vcol(e, w->line, w->col + 1) - 1;
	w->vc.line = w->line; w->vc.col = w->col; w->vc.v = w->want;
	w->vc.mode = e->mode; w->vc.ok = 1;
}

static size_t digits(size_t n) { size_t d = 1; while (n >= 10) { n /= 10; d++; } return d; }

static void put_num(Grid *g, int r, size_t num, int w, int left)
{
	char b[32];
	if (snprintf(b, sizeof b, left ? "%-*zu " : "%*zu ", w, num) < (int)sizeof b)
		grid_puts(g, r, 0, b, ATTR_DIM);
}

/* The cursor's screen column and the number column's width. */
static size_t cursor_vcol(Editor *e)
{
	size_t cv = core_vcol(e, e->win->line, e->win->col);
	if (e->mode == MODE_NORMAL) {     /* on a tab the cursor sits at its end, as Vim */
		size_t n; const unsigned char *s = core_line(e, e->win->line, &n);
		if (e->win->col < n && s[e->win->col] == '\t') cv += (size_t)cellw(s, n, e->win->col, cv) - 1;
	}
	return cv;
}

static int numw(Editor *e) { size_t d = digits(core_lines(e)); return d < 3 ? 3 : (int)d; }

void core_update(Editor *e)
{
	Win *w = e->win;
	size_t rows = (size_t)w->h, nl = core_lines(e);
	int tc = e->cols - numw(e) - 1;   /* text columns */
	if (rows < 1 || tc < 1) return;
	/* scrolloff, as far as the window allows */
	size_t so = rows > 2 * SCROLLOFF ? SCROLLOFF : (rows - 1) / 2;
	if (w->line < w->top + so) w->top = w->line > so ? w->line - so : 0;
	size_t below = nl - 1 - w->line < so ? nl - 1 - w->line : so;
	if (w->line + below >= w->top + rows) w->top = w->line + below + 1 - rows;
	size_t cv = cursor_vcol(e);
	if (cv < w->left) w->left = cv;
	if (cv >= w->left + (size_t)tc) w->left = cv - (size_t)tc + 1;
}

/* The thin editor has one buffer and one window. */
static int refuse(Editor *e) { core_msg(e, "E36: Not enough room"); return -1; }
int  core_edit_file(Editor *e, const char *path, int force) { (void)path; (void)force; return refuse(e); }
int  core_buf_goto(Editor *e, int nr) { return nr == 1 ? 0 : refuse(e); }
int  core_buf_next(Editor *e, long n) { (void)e; (void)n; return 0; }
int  core_buf_alt(Editor *e) { core_msg(e, "E23: No alternate file"); return -1; }
int  core_new(Editor *e, int vertical) { (void)vertical; return refuse(e); }
int  core_buf_delete(Editor *e, int nr, int force) { (void)nr; (void)force; return refuse(e); }
int  core_split(Editor *e, int vertical, int size) { (void)vertical; (void)size; return refuse(e); }
int  core_close(Editor *e, int force)
{
	if (!force && core_modified(e->buf)) {
		core_msg(e, "E37: No write since last change (add ! to override)");
		return -1;
	}
	e->quit = 1;
	return 0;
}
int  core_only(Editor *e) { (void)e; return 0; }
int  core_win_goto(Editor *e, int how, long count) { (void)e; (void)how; (void)count; return -1; }
int  core_win_move(Editor *e, int how, long count) { (void)how; (void)count; return refuse(e); }
void core_resize(Editor *e, int vertical, long n, int set) { (void)e; (void)vertical; (void)n; (void)set; }
void core_equal(Editor *e) { (void)e; }
void core_scroll(Editor *e, long n) { (void)e; (void)n; }
void core_scroll_half(Editor *e, int dir, long count) { (void)e; (void)dir; (void)count; }
void core_scroll_page(Editor *e, int dir, long count) { (void)e; (void)dir; (void)count; }
void core_scroll_cursor(Editor *e, int how) { (void)e; (void)how; }
size_t core_screen_line(Editor *e, int how, long count) { (void)how; (void)count; return e->win->line; }
int  core_screen_move(Editor *e, long n) { (void)e; (void)n; return -1; }
void core_pum(Editor *e, const PumItem *items, int n, int sel, size_t col) { (void)e; (void)items; (void)n; (void)sel; (void)col; }

void core_draw(Editor *e, Grid *g)
{
	e->rows = g->rows; e->cols = g->cols;
	e->win->h = g->rows - 2;          /* status line, command line */
	e->win->w = g->cols;
	int rows = e->win->h, nw = numw(e), tc = g->cols - nw - 1;
	if (rows < 1 || tc < 1) return;
	size_t nl = core_lines(e);
	core_update(e);
	size_t cv = cursor_vcol(e);
	struct Syntax *syn = e->buf->syn;

	grid_clear(g);
	for (int r = 0; r < rows; r++) {
		size_t l = e->win->top + (size_t)r;
		if (l >= nl) { grid_puts(g, r, 0, "~", ATTR_DIM); continue; }
		if (l == e->win->line) put_num(g, r, l + 1, nw, 1);
		else put_num(g, r, l > e->win->line ? l - e->win->line : e->win->line - l, nw, 0);
		size_t n, v = 0, ls = core_line_start(e, l), f0 = (size_t)-1;
		const unsigned char *s = core_line(e, l, &n);
		/* faces of bytes [f0, f0 + 1024) of the line; the thin editor also
		   asks for the bytes left of the view, where syntax.h promises only
		   the visible ones: package 4's core asks for those alone */
		unsigned char fc[1024];
		for (size_t i = 0; i < n && v < e->win->left + (size_t)tc; i += uni_len(s, n, i)) {
			if (syn && (f0 == (size_t)-1 || i >= f0 + sizeof fc || i < f0)) {
				f0 = i;
				syntax_faces(syn, ls + f0, ls + (n - f0 < sizeof fc ? n : f0 + sizeof fc), fc);
			}
			int w = cellw(s, n, i, v);
			size_t k = uni_len(s, n, i);
			unsigned cp = uni_cp(s, n, i);
			for (int j = 0; j < w; j++, v++) {
				if (v < e->win->left || v >= e->win->left + (size_t)tc) continue;
				int c = nw + 1 + (int)(v - e->win->left);
				unsigned char ctl[1] = { j ? (unsigned char)(s[i] ^ 0x40) : '^' };
				if (cp == '\t') grid_put(g, r, c, (const unsigned char *)" ", 1, 1, ATTR_NONE);
				else if (cp < 0x20 || cp == 0x7f) grid_put(g, r, c, ctl, 1, 1, ATTR_NONE);
				else if (j) continue;
				else if (cp == 0xFFFD && k == 1) grid_put(g, r, c, (const unsigned char *)"\xEF\xBF\xBD", 3, 1, ATTR_NONE);
				else grid_put(g, r, c, s + i, k, w, ATTR_NONE);
				if (syn) g->c[r * g->cols + c].face = fc[i - f0];
			}
		}
	}
	char st[512];
	int len = snprintf(st, sizeof st, " %s%s", e->buf->path ? e->buf->path : "[No Name]", core_modified(e->buf) ? " [+]" : "");
	for (int c = 0; c < g->cols; c++) grid_put(g, rows, c, (const unsigned char *)" ", 1, 1, ATTR_REV);
	grid_puts(g, rows, 0, st, ATTR_REV);
	snprintf(st, sizeof st, "%zu,%zu ", e->win->line + 1, e->win->col + 1);
	int pc = g->cols - (int)strlen(st);
	if (pc > len) grid_puts(g, rows, pc, st, ATTR_REV);
	if (e->mode == MODE_CMDLINE) {
		char t[2] = { (char)e->cmdtype, 0 };
		int c = grid_puts(g, rows + 1, 0, t, ATTR_NONE);
		const unsigned char *s = (const unsigned char *)e->cmd;
		for (size_t i = 0; i < e->cmdlen; i += uni_len(s, e->cmdlen, i))
			c += grid_put(g, rows + 1, c, s + i, uni_len(s, e->cmdlen, i), 1, ATTR_NONE);
		g->crow = rows + 1;
		g->ccol = c < g->cols ? c : g->cols - 1;
		return;
	}
	grid_puts(g, rows + 1, 0, e->mode == MODE_INSERT ? "-- INSERT --" : e->msg, ATTR_NONE);
	g->crow = (int)(e->win->line - e->win->top);
	g->ccol = nw + 1 + (int)(cv - e->win->left);
}
