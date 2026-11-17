/* Reference core: one buffer, one window, no wrapping. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "uni.h"

enum { SCROLLOFF = 6, TABSTOP = 8 };   /* FEATURES.md 1; Vim's tabstop */

int core_open(Editor *e, const char *path)
{
	char *data = NULL;
	size_t n = 0;
	memset(e, 0, sizeof *e);
	e->path = strdup(path);
	if (!e->path) return -1;
	if (file_load(path, &data, &n, &e->id)) {
		core_msg(e, "\"%s\": cannot read", path);
		return -1;
	}
	e->noeol = n == 0 || data[n - 1] != '\n';
	e->flags = n ? 0 : CORE_EMPTY;
	e->text = text_new(data, n);
	free(data);
	e->undo = undo_new();
	if (!e->text || !e->undo || (e->noeol && text_insert(e->text, n, "\n", 1))) {
		core_msg(e, "out of memory");
		return -1;
	}
	core_set_want(e);
	core_msg(e, "\"%s\"%s %zuL, %zuB", path, e->id.exists ? "" : " [New]", core_lines(e), n);
	return 0;
}

void core_free(Editor *e)
{
	text_free(e->text);
	undo_free(e->undo);
	free(e->path);
	free(e->lb);
}

size_t core_lines(const Editor *e) { return text_lines(e->text); }
size_t core_line_start(const Editor *e, size_t l) { return text_line_start(e->text, l); }

size_t core_line_len(const Editor *e, size_t l)
{
	return text_line_start(e->text, l + 1) - text_line_start(e->text, l) - 1;
}

const unsigned char *core_line(Editor *e, size_t l, size_t *len)
{
	size_t n = core_line_len(e, l);
	if (n + 1 > e->lbcap) {
		unsigned char *b = realloc(e->lb, n + 1);
		if (!b) { *len = 0; return (const unsigned char *)""; }
		e->lb = b; e->lbcap = n + 1;
	}
	*len = text_read(e->text, core_line_start(e, l), (char *)e->lb, n);
	return e->lb;
}

static int edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins, unsigned after)
{
	char *del = malloc(ndel ? ndel : 1);
	UndoCursor c = { e->line, e->col, e->flags };
	int r = -1;
	if (del && text_read(e->text, pos, del, ndel) == ndel
	    && !text_delete(e->text, pos, ndel)) {
		if (!text_insert(e->text, pos, ins, nins)) {
			if (!undo_record(e->undo, pos, del, ndel, ins, nins, c, after)) r = 0;
			else { text_delete(e->text, pos, nins); text_insert(e->text, pos, del, ndel); }
		} else text_insert(e->text, pos, del, ndel);
	}
	free(del);
	if (r) core_msg(e, "out of memory");
	/* an empty file saves empty until it gets a line of text (Vim) */
	else {
		e->flags = after;
		if (e->noeol && !e->id.size) e->noeol = 0;
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

/* Neovim's cursor after undo and redo (undo.c, u_undoredo): back where the
   change started if that is inside or next to the changed lines, else the
   first changed line at its first non-blank. */
int core_undo(Editor *e, int redo)
{
	UndoWhere w;
	size_t before = core_lines(e), c = e->line;
	int r = redo ? undo_redo(e->undo, e->text, &w) : undo_undo(e->undo, e->text, &w);
	if (r <= 0) {
		core_msg(e, r ? "out of memory" : redo ? "Already at newest change" : "Already at oldest change");
		return -1;
	}
	size_t n = core_lines(e);
	size_t first = text_line_of(e->text, w.lo), last;
	e->flags = w.flags;
	int at_start = w.lo == core_line_start(e, first);
	if (w.hi == w.lo && at_start) last = first - 1;   /* whole lines removed: none left */
	else last = text_line_of(e->text, w.hi > w.lo ? w.hi - 1 : w.lo);
	size_t l = w.cur.line + 1 >= first && w.cur.line <= last + 1 ? w.cur.line : first;
	if (l >= n) l = n - 1;                          /* check_cursor_lnum() */
	if (w.cur.line + 1 == l && l > 0) l--;          /* "o": back where it started */
	e->line = l;
	/* else beginline(BL_SOL | BL_FIX), which with 'nostartofline' is
	   coladvance(curswant): the column wanted before the command, since
	   curswant is recomputed only once the whole count is done */
	e->col = w.cur.line == l ? w.cur.col : core_col_at(e, l, e->want, 0);
	core_clamp(e);
	core_msg(e, "");
	/* u_undoredo() deletes the old lines, check_cursor_lnum(), inserts the
	   new ones, then changed_lines(): that invalidates the cached cursor
	   column only if the clamped cursor is at or below the first changed
	   line, which fails when the old lines ran to the end of the buffer */
	size_t kept = before - (n - (last + 1 - first));   /* lines the step replaced */
	if (c >= before - kept) c = before - kept ? before - kept - 1 : 0;
	return c >= first;
}

int core_modified(const Editor *e) { return undo_seq(e->undo) != e->saved_seq; }

int core_write(Editor *e, int force)
{
	char kept[4096];
	/* an empty buffer (ML_EMPTY) writes no bytes, not one empty line */
	size_t n = e->flags & CORE_EMPTY ? 0 : text_len(e->text) - (e->noeol ? 1 : 0);
	switch (file_save(e->path, e->text, n, &e->id, force, kept, sizeof kept)) {
	case SAVE_OK:
		e->saved_seq = undo_seq(e->undo);
		core_msg(e, "\"%s\" %zuL, %zuB written", e->path, core_lines(e), n);
		return 0;
	case SAVE_CHANGED:
		core_msg(e, "E13: \"%s\" changed on disk since it was read (add ! to override)", e->path);
		return -1;
	case SAVE_KEPT:
		e->saved_seq = undo_seq(e->undo);
		core_msg(e, "written, but another program replaced the file first; its version is in %s", kept);
		return 0;
	default:
		core_msg(e, "E212: Can't open file for writing: %s", e->path);
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
	if (e->line >= n) e->line = n - 1;
	const unsigned char *s = core_line(e, e->line, &len);
	if (e->mode == MODE_INSERT) { if (e->col > len) e->col = len; return; }
	if (e->col >= len) e->col = len ? uni_prev(s, len, len) : 0;
	else {
		/* a column restored by undo may fall inside a character: back to
		   its first byte, as check_cursor_col() does with mb_adjust_cursor() */
		size_t p = 0;
		for (size_t k; p + (k = uni_len(s, len, p)) <= e->col; p += k) {}
		e->col = p;
	}
}

void core_set_want(Editor *e)
{
	size_t n;
	const unsigned char *s = core_line(e, e->line, &n);
	e->want = core_vcol(e, e->line, e->col);
	if (e->mode == MODE_NORMAL && e->col < n && s[e->col] == '\t')
		e->want = core_vcol(e, e->line, e->col + 1) - 1;
}

static size_t digits(size_t n) { size_t d = 1; while (n >= 10) { n /= 10; d++; } return d; }

static void put_num(Grid *g, int r, size_t num, int w, int left)
{
	char b[32];
	if (snprintf(b, sizeof b, left ? "%-*zu " : "%*zu ", w, num) < (int)sizeof b)
		grid_puts(g, r, 0, b, ATTR_DIM);
}

void core_draw(Editor *e, Grid *g)
{
	int rows = g->rows - 2;           /* status line, command line */
	if (rows < 1) return;
	size_t nl = core_lines(e);
	int nw = (int)digits(nl) < 3 ? 3 : (int)digits(nl);
	int tc = g->cols - nw - 1;        /* text columns */
	if (tc < 1) return;
	/* scrolloff, as far as the window allows */
	size_t so = (size_t)rows > 2 * SCROLLOFF ? SCROLLOFF : (size_t)(rows - 1) / 2;
	if (e->line < e->top + so) e->top = e->line > so ? e->line - so : 0;
	size_t below = nl - 1 - e->line < so ? nl - 1 - e->line : so;
	if (e->line + below >= e->top + (size_t)rows) e->top = e->line + below + 1 - (size_t)rows;
	size_t cv = core_vcol(e, e->line, e->col);
	if (e->mode == MODE_NORMAL) {     /* on a tab the cursor sits at its end, as Vim */
		size_t n; const unsigned char *s = core_line(e, e->line, &n);
		if (e->col < n && s[e->col] == '\t') cv += (size_t)cellw(s, n, e->col, cv) - 1;
	}
	if (cv < e->left) e->left = cv;
	if (cv >= e->left + (size_t)tc) e->left = cv - (size_t)tc + 1;

	grid_clear(g);
	for (int r = 0; r < rows; r++) {
		size_t l = e->top + (size_t)r;
		if (l >= nl) { grid_puts(g, r, 0, "~", ATTR_DIM); continue; }
		if (l == e->line) put_num(g, r, l + 1, nw, 1);
		else put_num(g, r, l > e->line ? l - e->line : e->line - l, nw, 0);
		size_t n, v = 0;
		const unsigned char *s = core_line(e, l, &n);
		for (size_t i = 0; i < n && v < e->left + (size_t)tc; i += uni_len(s, n, i)) {
			int w = cellw(s, n, i, v);
			size_t k = uni_len(s, n, i);
			unsigned cp = uni_cp(s, n, i);
			for (int j = 0; j < w; j++, v++) {
				if (v < e->left || v >= e->left + (size_t)tc) continue;
				int c = nw + 1 + (int)(v - e->left);
				unsigned char ctl[1] = { j ? (unsigned char)(s[i] ^ 0x40) : '^' };
				if (cp == '\t') grid_put(g, r, c, (const unsigned char *)" ", 1, 1, ATTR_NONE);
				else if (cp < 0x20 || cp == 0x7f) grid_put(g, r, c, ctl, 1, 1, ATTR_NONE);
				else if (j) continue;
				else if (cp == 0xFFFD && k == 1) grid_put(g, r, c, (const unsigned char *)"\xEF\xBF\xBD", 3, 1, ATTR_NONE);
				else grid_put(g, r, c, s + i, k, w, ATTR_NONE);
			}
		}
	}
	char st[512];
	int len = snprintf(st, sizeof st, " %s%s", e->path, core_modified(e) ? " [+]" : "");
	for (int c = 0; c < g->cols; c++) grid_put(g, rows, c, (const unsigned char *)" ", 1, 1, ATTR_REV);
	grid_puts(g, rows, 0, st, ATTR_REV);
	snprintf(st, sizeof st, "%zu,%zu ", e->line + 1, e->col + 1);
	int pc = g->cols - (int)strlen(st);
	if (pc > len) grid_puts(g, rows, pc, st, ATTR_REV);
	if (e->mode == MODE_CMDLINE) {
		int c = grid_puts(g, rows + 1, 0, ":", ATTR_NONE);
		const unsigned char *s = (const unsigned char *)e->cmd;
		for (size_t i = 0; i < e->cmdlen; i += uni_len(s, e->cmdlen, i))
			c += grid_put(g, rows + 1, c, s + i, uni_len(s, e->cmdlen, i), 1, ATTR_NONE);
		g->crow = rows + 1;
		g->ccol = c < g->cols ? c : g->cols - 1;
		return;
	}
	grid_puts(g, rows + 1, 0, e->mode == MODE_INSERT ? "-- INSERT --" : e->msg, ATTR_NONE);
	g->crow = (int)(e->line - e->top);
	g->ccol = nw + 1 + (int)(cv - e->left);
}
