/* Buffers: their text, undo, saving, and moving between them. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_int.h"
#include "syntax.h"
#include "uni.h"

enum { DEL = -1 };                 /* mark_adjust()'s MAXLNUM: the lines are deleted */
static int reloading;              /* :e replaces the text but keeps the cursors */
static int top_file_num;           /* the last buffer number (Neovim's), never reused */
/* the line of the entry buflist_new() gives a buffer: lnum 0, no window */
static const size_t UNSET = (size_t)-1;

/* clear_wininfo() and the entry buflist_new() pushes for the window w:
   lnum 0, no options (wrap -1) */
static void wininfo_clear(Buf *b)
{
	for (WinInfo *i = b->wi, *nx; i; i = nx) { nx = i->next; free(i); }
	b->wi = NULL;
}

static void wininfo_reset(Buf *b, Win *w)
{
	wininfo_clear(b);
	if ((b->wi = calloc(1, sizeof *b->wi))) {   /* else only a position is lost */
		b->wi->win = w;
		b->wi->line = UNSET;
		b->wi->wrap = -1;
	}
}

size_t buf_lines(const Buf *b) { return text_lines(b->text); }
size_t core_lines(const Editor *e) { return buf_lines(e->buf); }
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

void lit_init(LineIt *it, const Buf *b, size_t l)
{
	it->t = b->text;
	it->pos = text_line_start(b->text, l);
	it->end = text_line_start(b->text, l + 1) - 1;
	it->i = it->n = 0;
}

size_t lit_next(LineIt *it, const unsigned char **s)
{
	if (it->n - it->i < 4 && it->pos < it->end) {
		size_t k = it->n - it->i, want = sizeof it->b - k;
		memmove(it->b, it->b + it->i, k);
		if (want > it->end - it->pos) want = it->end - it->pos;
		it->n = k + text_read(it->t, it->pos, (char *)it->b + k, want);
		it->pos += want;
		it->i = 0;
	}
	if (it->i >= it->n) return 0;
	size_t k = uni_len(it->b, it->n, it->i);
	*s = it->b + it->i;
	it->i += k;
	return k;
}

int core_cellw(const unsigned char *s, size_t n, size_t i, size_t v)
{
	if (s[i] == '\t') return TABSTOP - (int)(v % TABSTOP);
	if (s[i] < 0x20 || s[i] == 0x7f) return 2;                      /* ^X */
	if (s[i] >= 0x80 && uni_len(s, n, i) == 1) return 4;            /* <xx> */
	return uni_width(uni_cp(s, n, i));
}

size_t buf_vcol_end(const Buf *b, size_t l)
{
	LineIt it;
	const unsigned char *s;
	size_t v = 0, k;
	lit_init(&it, b, l);
	while ((k = lit_next(&it, &s))) v += (size_t)core_cellw(s, k, 0, v);
	return v;
}

/* ---- opening and closing buffers ---- */

/* every change reaches the syntax tree before it is made (UndoHook) */
static void undo_seen(void *ctx, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	Buf *b = ctx;
	if (b->syn) syntax_edit(b->syn, t, pos, ndel, ins, nins);
}

/* :bd leaves a buffer unlisted (b_p_bl off) and unloaded, its number and
   name kept; :b N loads it again without listing it, CTRL-^ and :e list
   it again (measured in nvim --headless, 11 runs).  e->bufs holds the
   listed buffers, this list the others, both by number. */
static Buf *unlisted;

Buf *buf_unlisted(void) { return unlisted; }

static Buf **buf_slot(Buf **p, int nr)
{
	while (*p && (*p)->nr < nr) p = &(*p)->next;
	return p;
}

static Buf *buf_last(Editor *e)
{
	Buf *b = e->bufs;
	while (b->next) b = b->next;
	return b;
}

static void buf_unlink(Buf **p, Buf *b)
{
	while (*p != b) p = &(*p)->next;
	*p = b->next;
}

static int buf_listed(const Editor *e, const Buf *b)
{
	for (const Buf *x = e->bufs; x; x = x->next) if (x == b) return 1;
	return 0;
}

/* buflist_findnr(): listed or not */
static Buf *buf_find(Editor *e, int nr)
{
	for (int k = 0; k < 2; k++)
		for (Buf *b = k ? unlisted : e->bufs; b; b = b->next) if (b->nr == nr) return b;
	return NULL;
}

/* Loads path into b, a buffer without text (path NULL: empty, no name).
   -1 on error, msg set, b as it was. */
static int buf_load(Editor *e, Buf *b, const char *path)
{
	char *data = NULL, *name = NULL;
	size_t n = 0;
	FileId id = { 0 };
	Text *t;
	Undo *u;
	if (path) {
		if (!(name = strdup(path))) goto oom;
		if (file_load(path, &data, &n, &id)) {
			core_msg(e, "\"%s\": cannot read", path);
			free(name);
			return -1;
		}
	}
	int noeol = n == 0 || data[n - 1] != '\n';
	t = text_new(data, n);
	free(data);
	u = undo_new();
	if (!t || !u || (noeol && text_insert(t, n, "\n", 1))) {
		text_free(t);
		undo_free(u);
		free(name);
		goto oom;
	}
	b->text = t;
	b->undo = u;
	b->path = name;
	b->id = id;
	b->noeol = noeol;
	b->flags = n ? 0 : CORE_EMPTY;
	b->saved_seq = 0;
	b->syn = path ? syntax_open(path, t) : NULL;
	undo_hook(u, undo_seen, b);
	if (path) {
		/* readfile()'s message */
		if (!id.exists) core_msg(e, "\"%s\" [New]", path);
		else core_msg(e, "\"%s\"%s %zuL, %zuB", path, noeol && n ? " [noeol]" : "", n ? buf_lines(b) : 0, n);
	}
	return 0;
oom:
	core_msg(e, "out of memory");
	return -1;
}

/* A new buffer on path (NULL: empty, no name), numbered after the last.
   NULL on error, msg set. */
static Buf *buf_new(Editor *e, const char *path)
{
	Buf *b = calloc(1, sizeof *b), **p = &e->bufs;
	if (!b) { core_msg(e, "out of memory"); return NULL; }
	if (buf_load(e, b, path)) { free(b); return NULL; }
	while (*p) p = &(*p)->next;
	b->nr = ++top_file_num;
	*p = b;
	wininfo_reset(b, e->win);
	return b;
}

static void buf_unload(Buf *b)
{
	syntax_free(b->syn);
	text_free(b->text);
	undo_free(b->undo);
	free(b->path);
}

/* the file read again into an unloaded buffer (enter_buffer()'s
   open_buffer()); -1 msg set */
static int buf_reload(Editor *e, Buf *b)
{
	char *path = b->path;
	if (buf_load(e, b, path)) return -1;
	free(path);
	return 0;
}

/* buflist_new() with BLN_LISTED: b goes back in the list */
static void buf_list(Editor *e, Buf *b)
{
	if (buf_listed(e, b)) return;
	buf_unlink(&unlisted, b);
	Buf **p = buf_slot(&e->bufs, b->nr);
	b->next = *p;
	*p = b;
}

static void buf_free(Buf *b)
{
	wininfo_clear(b);
	buf_unload(b);
	free(b);
}

int core_open(Editor *e, const char *path)
{
	memset(e, 0, sizeof *e);
	top_file_num = 0;
	e->rows = 24; e->cols = 80;
	Win *w = calloc(1, sizeof *w);
	e->win = w;
	if (!w || !(e->buf = buf_new(e, path))) { free(w); e->win = NULL; return -1; }
	e->wins = w;
	w->buf = e->buf;
	layout_init(e, w);
	if (!e->layout) { core_msg(e, "out of memory"); return -1; }
	core_set_want(e);
	return 0;
}

void core_free(Editor *e)
{
	for (int k = 0; k < 2; k++)
		for (Buf *b = k ? unlisted : e->bufs, *nx; b; b = nx) { nx = b->next; buf_free(b); }
	unlisted = NULL;
	for (Win *w = e->wins, *nx; w; w = nx) { nx = w->next; free(w); }
	layout_free(e->layout);
	free(e->lb);
	memset(e, 0, sizeof *e);
}

/* ---- the last cursor per window (buflist_setfpos(), find_wininfo()) ---- */

/* buflist_setfpos(), options copied.  close_buffer() passes lnum 0 for
   a cursor on line 1, which leaves an entry's position as it was. */
void core_wininfo_set(Buf *b, Win *w, int close)
{
	WinInfo **p = &b->wi, *i;
	while (*p && (*p)->win != w) p = &(*p)->next;
	if ((i = *p)) *p = i->next;
	else if (!(i = calloc(1, sizeof *i))) return;   /* only a remembered position is lost */
	else close = 0;
	i->win = w;
	if (!close || w->line) { i->line = w->line; i->col = w->col; }
	i->wrap = w->wrap;
	i->next = b->wi;
	b->wi = i;
}

/* find_wininfo(): this window's entry, else the most recent; with opts
   only entries with options, or of a window that shows b (get_winopts()) */
static WinInfo *wininfo_find(Buf *b, Win *w, int opts)
{
	for (WinInfo *i = b->wi; i; i = i->next) if (i->win == w && (!opts || i->wrap >= 0)) return i;
	if (!opts) return b->wi;
	for (WinInfo *i = b->wi; i; i = i->next) if (i->wrap >= 0 || (i->win && i->win->buf == b)) return i;
	return NULL;
}

/* readfile() leaves the cursor on line 1's first non-blank (BL_FIX);
   do_ecmd() keeps that only when it moved from the emptied buffer's (1,0)
   and is off skipwhite()'s column: a line of two or more blanks
   (measured: "   " goes to 1,2; " ", "\t", "" and "  x" keep the line) */
static int line1_blank(Editor *e)
{
	size_t n, c = core_first_nonblank(e, 0);
	const unsigned char *s = core_line(e, 0, &n);
	return c > 0 && c < n && (s[c] == ' ' || s[c] == '\t');
}

/* How a buffer is entered: :b (set_curbuf()), :e (do_ecmd()), :new
   (do_ecmd() without oldwin), CTRL-^ (buflist_getfile()). */
enum { ENTER_B, ENTER_E, ENTER_NEW, ENTER_ALT };

/* enter_buffer(): show b in the current window, the cursor where this
   window (or else the last one) had it, the line in the middle.  -1 if
   an unloaded buffer cannot be read (msg set).  The cursor, measured in
   nvim --headless (51 probe runs): an entry never set (lnum 0) means
   line 1, column 0 for :b (buflist_getfpos()), the first non-blank for
   :e and CTRL-^ (do_ecmd() with newlnum 0); no entry at all (after :bd)
   means line 1, column 0, except the first non-blank for :e and, for
   CTRL-^, readfile()'s column on a line of blanks. */
static int enter_buffer(Editor *e, Buf *b, int how)
{
	Win *w = e->win;
	if (!b->text && buf_reload(e, b)) return -1;
	if (how == ENTER_E || how == ENTER_ALT) buf_list(e, b);   /* BLN_LISTED */
	if (w->buf != b) {
		if (how != ENTER_NEW) core_wininfo_set(w->buf, w, 0);   /* buflist_altfpos() */
		w->alt = w->buf;
	}
	w->buf = e->buf = b;
	w->vc.ok = 0;                      /* w_valid = 0 */
	/* get_winopts(): from the window that shows b now, else as b was
	   left; with neither, the window keeps its own (the global value) */
	WinInfo *i = wininfo_find(b, w, 1);
	if (i && i->win && i->win != w && i->win->buf == b) w->wrap = i->win->wrap;
	else if (i && i->wrap >= 0) w->wrap = i->wrap;
	i = wininfo_find(b, w, 0);
	w->line = w->col = 0;
	/* didset_window_options(): one of leftcol and skipcol goes, as for :set wrap */
	if (w->wrap) w->left = 0;
	else w->skipcol = 0;
	if (i && i->line != UNSET) {
		w->line = i->line;
		w->col = i->col;       /* nostartofline */
	} else if (i ? how != ENTER_B : how == ENTER_E || (how == ENTER_ALT && line1_blank(e)))
		w->col = core_first_nonblank(e, 0);
	win_check_cursor(w);
	core_set_want(e);
	if (how != ENTER_B) { win_ecmd_topline(e, w); return 0; }
	w->top = 0;
	w->frame->s.topvalid = 0;          /* w_valid = 0, w_valid_cursor kept */
	w->frame->s.bv = 0;
	scroll_halfway(e, w, 0, 0);
	return 0;
}

/* hidden is on: a buffer stays loaded when no window shows it. */
static int buf_switch(Editor *e, Buf *b, int how)
{
	return b == e->buf ? 0 : enter_buffer(e, b, how);
}

static Buf *buf_by_path(Editor *e, const char *path)
{
	for (int k = 0; k < 2; k++)
		for (Buf *b = k ? unlisted : e->bufs; b; b = b->next)
			if (b->path && !strcmp(b->path, path)) return b;
	return NULL;
}

static int edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins, unsigned after);

/* beginline(BL_WHITE | BL_FIX): do_ecmd() puts the cursor on the first
   non-blank only in a buffer it loads now; a loaded buffer gets its last
   position, a reload keeps the cursor (nostartofline) */
static void beginline_white(Editor *e)
{
	e->win->col = core_first_nonblank(e, e->win->line);
	core_set_want(e);
}

/* buf_is_empty() */
static int buf_empty(const Buf *b)
{
	return text_len(b->text) == 1;
}

static int edit_file(Editor *e, const char *path, int force, int split);

int core_edit_file(Editor *e, const char *path, int force) { return edit_file(e, path, force, 0); }

/* :new and :vnew: do_ecmd() after the split has no oldwin, so the new
   window keeps no position in the buffer it was split from */
int core_new(Editor *e, int vertical)
{
	return core_split(e, vertical, 0) ? -1 : edit_file(e, "", 0, 1);
}

/* split: the window was split for this command (no buflist_altfpos()) */
static int edit_file(Editor *e, const char *path, int force, int split)
{
	Buf *b = e->buf;
	Win *w = e->win;
	if (!path || (*path && b->path && !strcmp(b->path, path))) {   /* :e, :e!, :e on its own name */
		if (!b->path) { core_msg(e, "E32: No file name"); return -1; }
		if (!force && core_modified(b)) {
			core_msg(e, "E37: No write since last change (add ! to override)");
			return -1;
		}
		char *data;
		size_t n, len = text_len(b->text);
		FileId id;
		if (file_load(b->path, &data, &n, &id)) { core_msg(e, "\"%s\": cannot read", b->path); return -1; }
		/* the whole text replaced as one undo step, as 'undoreload' does;
		   the cursors stay on their lines (check_lnums()) */
		int noeol = n == 0 || data[n - 1] != '\n';
		char *d = noeol ? realloc(data, n + 1) : data;
		int r = -1;
		if (d) {
			data = d;
			if (noeol) data[n] = '\n';
			reloading = 1;
			r = edit(e, 0, len, data, n + (size_t)noeol, n ? 0 : CORE_EMPTY);
			reloading = 0;
		}
		free(data);
		if (r) { core_msg(e, "out of memory"); return -1; }
		undo_close(b->undo);
		b->id = id;
		b->noeol = noeol;
		b->saved_seq = undo_seq(b->undo);
		for (Win *v = e->wins; v; v = v->next) if (v->buf == b) win_check_cursor(v);
		if (line1_blank(e)) { w->line = 0; w->col = core_first_nonblank(e, 0); }
		core_set_want(e);
		win_ecmd_topline(e, w);
		core_msg(e, "\"%s\" %zuL, %zuB", b->path, n ? buf_lines(b) : 0, n);
		return 0;
	}
	Buf *to = *path ? buf_by_path(e, path) : NULL, *prev_alt = w->alt;
	if (to == b) return 0;
	/* buflist_new() reuses an empty, unchanged buffer without a name that
	   no other window shows (curbuf_reusable()); the alternate becomes the
	   buffer itself unless there was one */
	int shown = 0;
	for (Win *v = e->wins; v; v = v->next) shown += v->buf == b;
	if (!to && !b->path && shown <= 1 && buf_empty(b) && !core_modified(b)) {
		if (*path) {
			Buf old = *b;
			if (buf_load(e, b, path)) return -1;
			b->wi = old.wi;
			wininfo_reset(b, w);
			buf_unload(&old);
			w->line = w->col = w->top = w->skipcol = w->left = 0;
		}
		w->alt = prev_alt ? prev_alt : b;
		beginline_white(e);
		win_ecmd_topline(e, w);
		return 0;
	}
	if (!to && !(to = buf_new(e, *path ? path : NULL))) return -1;
	return enter_buffer(e, to, split ? ENTER_NEW : ENTER_E);
}

/* CTRL-^: buflist_getfile() goes through do_ecmd(), so a position never
   set means the first non-blank */
int core_buf_alt(Editor *e)
{
	Buf *b = e->win->alt;
	if (!b) { core_msg(e, "E23: No alternate file"); return -1; }
	return buf_switch(e, b, ENTER_ALT);
}

int core_buf_goto(Editor *e, int nr)
{
	Buf *b = buf_find(e, nr);
	if (!b) { core_msg(e, "E86: Buffer %d does not exist", nr); return -1; }
	return buf_switch(e, b, ENTER_B);
}

int core_buf_next(Editor *e, long n)
{
	Buf *b = e->buf;
	if (!buf_listed(e, b)) {
		/* do_buffer_ext(): from an unlisted buffer, :bn starts at the
		   first listed one after it, :bp at the last before it */
		Buf *p = *buf_slot(&e->bufs, b->nr), *q = NULL;
		for (Buf *x = e->bufs; x != p; x = x->next) q = x;
		b = n > 0 ? (p ? p : e->bufs) : (q ? q : buf_last(e));
		n += n > 0 ? -1 : 1;
	}
	for (long k = n < 0 ? -n : n; k > 0; k--) {
		if (n > 0) b = b->next ? b->next : e->bufs;
		else {                     /* the one before, wrapping to the last */
			Buf *p = e->bufs, *last = e->bufs;
			while (last->next) last = last->next;
			if (b == e->bufs) b = last;
			else { while (p->next != b) p = p->next; b = p; }
		}
	}
	return buf_switch(e, b, ENTER_B);
}

/* close_buffer() with DOBUF_DEL: unloaded and unlisted, the number and
   name kept, the positions gone (free_buffer_stuff()), the alternates
   that name it kept */
static void buf_unlist(Editor *e, Buf *b)
{
	syntax_free(b->syn);
	text_free(b->text);
	undo_free(b->undo);
	b->syn = NULL;
	b->text = NULL;
	b->undo = NULL;
	wininfo_clear(b);
	if (!buf_listed(e, b)) return;
	buf_unlink(&e->bufs, b);
	Buf **p = buf_slot(&unlisted, b->nr);
	b->next = *p;
	*p = b;
}

/* do_buffer_ext() with DOBUF_DEL */
int core_buf_delete(Editor *e, int nr, int force)
{
	Buf *b = nr ? buf_find(e, nr) : e->buf;
	if (!b || !b->text) { core_msg(e, "E516: No buffers were deleted"); return -1; }
	if (!force && core_modified(b)) {
		core_msg(e, "E89: No write since last change for buffer %d (add ! to override)", b->nr);
		return -1;
	}
	if (b == e->buf && (e->bufs == b && !b->next)) {
		/* empty_curbuf(): the other windows on b close, the current one gets
		   an empty buffer (unless b is one already) */
		for (Win *w = e->wins, *nx; w; w = nx) {
			nx = w->next;
			if (w->buf == b && w != e->win) win_close(e, w);
		}
		if (core_edit_file(e, "", 1)) return -1;
		if (e->buf != b) buf_unlist(e, b);
		return 0;
	}
	/* the windows on b close while they are not the last */
	while (e->win->buf == b && e->wins->next) win_close(e, e->win);
	if (e->win->buf != b) {
		for (Win *w = e->wins, *nx; w; w = nx) {
			nx = w->next;
			if (w->buf == b && e->wins->next) win_close(e, w);
		}
		buf_unlist(e, b);
		return 0;
	}
	/* the last window shows b: another listed buffer there.  Neovim takes
	   the newest file in the jump list (the keys', package 5), then the
	   next buffer, then the previous; here the alternate stands in for the
	   jump list (hypothesis: the same while the jump list's last file is
	   the alternate) */
	Buf *to = e->win->alt;
	if (!to || to == b || !buf_listed(e, to)) {
		to = *buf_slot(&e->bufs, b->nr + 1);
		if (!to) for (Buf *x = e->bufs; x && x->nr < b->nr; x = x->next) to = x;
	}
	if (enter_buffer(e, to, ENTER_B)) return -1;
	buf_unlist(e, b);
	return 0;
}

/* ---- editing ---- */

/* mark_adjust() for the other windows on b and every remembered cursor:
   lines [l1, l2) move by amount (DEL: deleted), lines from l2 on by after
   (an end past the range, so that an empty range at line 0 has one) */
static void adjust(Editor *e, Buf *b, size_t l1, size_t l2, long amount, long after)
{
	for (Win *w = e->wins; w; w = w->next) {
		if (w->buf != b || w == e->win) continue;
		if (w->top >= l1 && w->top < l2) {
			if (amount == DEL) w->top = l1 ? l1 - 1 : 0;
			else if (w->top > l1) w->top += (size_t)amount;
		} else if (after && w->top >= l2) w->top += (size_t)after;
		if (w->line >= l1 && w->line < l2) {
			if (amount == DEL) { w->line = l1 ? l1 - 1 : 0; w->col = 0; }
			else w->line += (size_t)amount;
		} else if (after && w->line >= l2) w->line += (size_t)after;
	}
	for (WinInfo *i = b->wi; i; i = i->next) {
		if (i->line == UNSET) continue;
		if (i->line >= l1 && i->line < l2) {
			if (amount == DEL) { i->line = l1 ? l1 - 1 : 0; i->col = 0; }
			else i->line += (size_t)amount;
		} else if (after && i->line >= l2) i->line += (size_t)after;
	}
}

static size_t newlines(const char *s, size_t n)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++) k += s[i] == '\n';
	return k;
}

/* The marks of an edit at pos that deleted d lines' newlines and inserted
   i: whole lines when it starts and ends at line starts (del_lines(),
   appended_lines_mark()), else lines joined to or split from line L. */
static void adjust_edit(Editor *e, size_t L, int at_start, int whole_del, int whole_ins, size_t d, size_t i)
{
	if (reloading) return;
	if (d) {
		if (at_start && whole_del) adjust(e, e->buf, L, L + d, DEL, -(long)d);
		else adjust(e, e->buf, L + 1, L + d + 1, DEL, -(long)d);
	}
	if (i) adjust(e, e->buf, at_start && whole_ins ? L : L + 1, (size_t)-1, (long)i, 0);
}

static int edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins, unsigned after)
{
	Buf *b = e->buf;
	/* nothing changes, so nothing is recorded: x on an empty line and dd
	   in an empty buffer leave the buffer unmodified, as in Neovim */
	if (!ndel && !nins && after == b->flags) return 0;
	char *del = malloc(ndel ? ndel : 1);
	UndoCursor c = { e->win->line, e->win->col, b->flags };
	int r = -1;
	size_t L = text_line_of(b->text, pos);
	int at_start = pos == text_line_start(b->text, L);
	if (del && text_read(b->text, pos, del, ndel) == ndel) undo_seen(b, b->text, pos, ndel, ins, nins);
	else { free(del); del = NULL; }
	if (del && !text_delete(b->text, pos, ndel)) {
		if (!text_insert(b->text, pos, ins, nins)) {
			if (!undo_record(b->undo, pos, del, ndel, ins, nins, c, after)) r = 0;
			else { text_delete(b->text, pos, nins); text_insert(b->text, pos, del, ndel); }
		} else text_insert(b->text, pos, del, ndel);
	}
	/* out of memory after the syntax tree heard of the change: it is
	   built again for the text as it was */
	if (r && del && b->syn) {
		syntax_free(b->syn);
		b->syn = syntax_open(b->path, b->text);
	}
	e->win->vc.ok = 0;
	/* changed_common(): a change at or before a window's cursor makes its
	   view be looked at again */
	if (!r)
		for (Win *w = e->wins; w; w = w->next)
			if (w->buf == b && (w->line > L || (w->line == L && w->col >= pos - text_line_start(b->text, L))))
				win_invalidate(w);
	if (!r) adjust_edit(e, L, at_start, ndel && del[ndel - 1] == '\n', nins && ins[nins - 1] == '\n',
	                    newlines(del, ndel), newlines(ins, nins));
	free(del);
	if (r) core_msg(e, "out of memory");
	/* an empty file saves empty until it gets a line of text (Vim) */
	else {
		b->flags = after;
		if (b->noeol && !b->id.size) b->noeol = 0;
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
	Win *win = e->win;
	size_t before = core_lines(e), c = win->line;
	int r = redo ? undo_redo(e->buf->undo, e->buf->text, &w) : undo_undo(e->buf->undo, e->buf->text, &w);
	if (r <= 0) {
		core_msg(e, r ? "out of memory" : redo ? "Already at newest change" : "Already at oldest change");
		return -1;
	}
	size_t n = core_lines(e);
	size_t first = text_line_of(e->buf->text, w.lo), last;
	for (Win *v = e->wins; v; v = v->next) if (v->buf == e->buf) win_invalidate(v);
	e->buf->flags = w.flags;
	int at_start = w.lo == core_line_start(e, first);
	if (w.hi == w.lo && at_start) last = first - 1;   /* whole lines removed: none left */
	else last = text_line_of(e->buf->text, w.hi > w.lo ? w.hi - 1 : w.lo);
	/* the other windows: the step replaced lines first.. by last - first + 1 */
	size_t kept = before - (n - (last + 1 - first));   /* lines the step replaced */
	if (n != before) adjust(e, e->buf, first, first + kept, DEL, (long)n - (long)before);
	size_t l = w.cur.line + 1 >= first && w.cur.line <= last + 1 ? w.cur.line : first;
	if (l >= n) l = n - 1;                          /* check_cursor_lnum() */
	if (w.cur.line + 1 == l && l > 0) l--;          /* "o": back where it started */
	win->line = l;
	/* else beginline(BL_SOL | BL_FIX), which with 'nostartofline' is
	   coladvance(curswant): the column wanted before the command, since
	   curswant is recomputed only once the whole count is done */
	win->col = w.cur.line == l ? w.cur.col : core_col_at(e, l, win->want, 0);
	core_clamp(e);
	core_msg(e, "");
	/* u_undoredo() deletes the old lines, check_cursor_lnum(), inserts the
	   new ones, then changed_lines(): that invalidates the cached cursor
	   column only if the clamped cursor is at or below the first changed
	   line, which fails when the old lines ran to the end of the buffer */
	if (c >= before - kept) c = before - kept ? before - kept - 1 : 0;
	if (c >= first) win->vc.ok = 0;
	return c >= first;
}

int core_modified(const Buf *b) { return b->undo && undo_seq(b->undo) != b->saved_seq; }

int core_write(Editor *e, Buf *b, int force)
{
	char kept[4096];
	if (!b->path) { core_msg(e, "E32: No file name"); return -1; }
	/* an empty buffer (ML_EMPTY) writes no bytes, not one empty line */
	size_t n = b->flags & CORE_EMPTY ? 0 : text_len(b->text) - (b->noeol ? 1 : 0);
	switch (file_save(b->path, b->text, n, &b->id, force, kept, sizeof kept)) {
	case SAVE_OK:
		b->saved_seq = undo_seq(b->undo);
		core_msg(e, "\"%s\" %zuL, %zuB written", b->path, n ? buf_lines(b) : 0, n);
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

/* ---- columns ---- */

size_t core_vcol(Editor *e, size_t l, size_t col)
{
	size_t n, v = 0;
	const unsigned char *s = core_line(e, l, &n);
	for (size_t i = 0; i < col && i < n; i += uni_len(s, n, i)) v += (size_t)core_cellw(s, n, i, v);
	return v;
}

size_t core_col_at(Editor *e, size_t l, size_t want, int past_end)
{
	size_t n, v = 0, i = 0, last = 0;
	const unsigned char *s = core_line(e, l, &n);
	while (i < n) {
		size_t w = (size_t)core_cellw(s, n, i, v);
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
	Win *w = e->win;
	size_t n = core_lines(e), len;
	if (w->line >= n) w->line = n - 1;
	const unsigned char *s = core_line(e, w->line, &len);
	if (e->mode == MODE_INSERT) { if (w->col > len) w->col = len; return; }
	if (w->col >= len) w->col = len ? uni_prev(s, len, len) : 0;
	else {
		/* a column restored by undo may fall inside a character: back to
		   its first byte, as check_cursor_col() does with mb_adjust_cursor() */
		size_t p = 0;
		for (size_t k; p + (k = uni_len(s, len, p)) <= w->col; p += k) {}
		w->col = p;
	}
}

void core_set_want(Editor *e)
{
	Win *w = e->win;
	/* w_virtcol kept while VALID_VIRTCOL holds (see core.h) */
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

/* :set wrap / nowrap: did_set_wrap() drops one of leftcol and skipcol;
   check_redraw_for() calls changed_window_setting() even when the value
   stays (measured: nowrap on a nowrap window recentres); setting a window
   option sets w_set_curswant unless after $ (option.c; measured) */
void core_set_wrap(Editor *e, int on)
{
	Win *w = e->win;
	w->wrap = on;
	if (on) w->left = 0;
	else w->skipcol = 0;
	win_invalidate(w);
	if (w->want != WANT_EOL) core_set_want(e);
}

size_t win_vcol(Editor *e, Win *w, size_t *start, size_t *end)
{
	LineIt it;
	const unsigned char *s;
	size_t v = 0, i = 0, k, cw = 1;
	lit_init(&it, w->buf, w->line);
	while ((k = lit_next(&it, &s))) {
		cw = (size_t)core_cellw(s, k, 0, v);
		if (i >= w->col) break;
		v += cw;
		i += k;
	}
	if (!k) cw = 1;                /* past the end: one cell */
	if (start) *start = v;
	if (end) *end = v + cw - 1;
	/* in normal mode the cursor shows on the last cell of a tab */
	return k && *s == '\t' && e->mode != MODE_INSERT ? v + cw - 1 : v;
}
