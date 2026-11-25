/* Windows and splits: Neovim's frame tree (window.c) with laststatus=2
   (every window has a status line), equalalways, splitbelow, splitright,
   winheight 1, winminheight 1, winwidth 20, winminwidth 1. */
#include <stdlib.h>
#include <string.h>
#include "core_int.h"

enum { WM = 1, STATUS = 1 };   /* winminheight, winminwidth */
/* Neovim has a function for each direction (frame_setheight(),
   frame_setwidth(), ...); here one function takes the axis a: ROWS for
   heights, COLS for widths. */
/* 'winheight' and 'winwidth': a split with a size sets them while it enters
   the new window, as win_split_ins() does */
static int p_wsz[2] = { 1, 20 };
enum { WSP_VERT = 1, WSP_TOP = 2, WSP_BOT = 4 };
typedef struct Frame Frame;

static Frame *frame_new(int layout)
{
	Frame *f = calloc(1, sizeof *f);
	if (f) f->layout = layout;
	return f;
}

void layout_free(Frame *f)
{
	for (Frame *c = f ? f->child : NULL, *nx; c; c = nx) { nx = c->next; layout_free(c); }
	free(f);
}

/* frame2win(): the top left window of f */
static Win *frame2win(Frame *f)
{
	while (!f->win) f = f->child;
	return f->win;
}

static int has_win(Frame *f, Win *w)
{
	if (f->win) return f->win == w;
	for (Frame *c = f->child; c; c = c->next) if (has_win(c, w)) return 1;
	return 0;
}

static int cmdheight(Editor *e) { return e->rows - e->layout->size[ROWS] - e->layout->top; }
/* tabline_height() with 'showtabline' 1: the terminal shows a tabline
   while there are two tab pages; an OS window per tab page shows none */
int tabline_height(const Editor *e) { return e->tabline && e->tabs->next; }

/* the layout whose children lie one after another on axis a */
static int along(int a) { return a == COLS ? FR_ROW : FR_COL; }
/* a leaf's status line or separator, part of its frame's size */
static int edge(const Frame *f, int a) { return a == COLS ? f->vsep : STATUS; }
static int *wsize(Win *w, int a) { return a == COLS ? &w->w : &w->h; }
static int *wpos(Win *w, int a) { return a == COLS ? &w->x : &w->y; }
static void fix_size(Win *w, int a) { w->frame->size[a] = *wsize(w, a) + edge(w->frame, a); }
static void win_new_size(Editor *e, Win *w, int size, int a)
{
	if (a == COLS) win_new_width(e, w, size);
	else win_new_height(e, w, size);
}

/* frame_minheight(), frame_minwidth(): next_curwin gets winheight or
   winwidth instead of the minimum */
static int minsize(Frame *f, Win *next, int a)
{
	if (f->win) return (f->win == next ? p_wsz[a] : WM) + edge(f, a);
	int m = 0;
	for (Frame *c = f->child; c; c = c->next) {
		int n = minsize(c, next, a);
		if (f->layout == along(a)) m += n;
		else if (n > m) m = n;
	}
	return m;
}

/* frame_new_height(), frame_new_width() */
static void new_size(Editor *e, Frame *f, int size, int first, int a)
{
	if (f->win) {
		if (a == COLS) {
			Frame *p = f;
			while (p->parent && !(p->parent->layout == FR_ROW && p->next)) p = p->parent;
			if (!p->parent) f->vsep = 0;
		}
		win_new_size(e, f->win, size - edge(f, a), a);
	} else if (f->layout != along(a)) {
		Frame *c;
		do {
			for (c = f->child; c; c = c->next) {
				new_size(e, c, size, first, a);
				if (c->size[a] > size) { size = c->size[a]; break; }
			}
		} while (c);
	} else {
		Frame *c = f->child;
		if (!first) while (c->next) c = c->next;
		int extra = size - f->size[a];
		if (extra < 0) {
			while (c) {
				int m = minsize(c, NULL, a);
				if (c->size[a] + extra < m) {
					extra += c->size[a] - m;
					new_size(e, c, m, first, a);
				} else {
					new_size(e, c, c->size[a] + extra, first, a);
					break;
				}
				c = first ? c->next : c->prev;
				if (!c) size -= extra;
			}
		} else if (extra > 0) new_size(e, c, c->size[a] + extra, first, a);
	}
	f->size[a] = size;
}

/* frame_set_vsep() */
static void set_vsep(Editor *e, Frame *f, int add)
{
	if (f->win) {
		if (add && !f->vsep) {
			if (f->win->w > 0) win_new_width(e, f->win, f->win->w - 1);
			f->vsep = 1;
		} else if (!add && f->vsep) {
			win_new_width(e, f->win, f->win->w + 1);
			f->vsep = 0;
		}
	} else if (f->layout == FR_COL) {
		for (Frame *c = f->child; c; c = c->next) set_vsep(e, c, add);
	} else {
		Frame *c = f->child;
		while (c->next) c = c->next;
		set_vsep(e, c, add);
	}
}

/* frame_comp_pos(), win_comp_pos() */
static void comp_pos(Frame *f, int *row, int *col)
{
	if (f->win) {
		Win *w = f->win;
		w->y = *row;
		w->x = *col;
		*row += w->h + STATUS > f->size[ROWS] ? f->size[ROWS] : w->h + STATUS;
		*col += w->w + f->vsep;
		return;
	}
	int r0 = *row, c0 = *col;
	for (Frame *c = f->child; c; c = c->next) {
		if (f->layout == FR_ROW) *row = r0;
		else *col = c0;
		comp_pos(c, row, col);
	}
	/* the frame is a rectangle: what follows starts past all of it */
	if (f->layout == FR_ROW) *row = r0 + f->size[ROWS];
	else *col = c0 + f->size[COLS];
}

static void win_comp_pos(Editor *e) { int r = e->layout->top, c = 0; comp_pos(e->layout, &r, &c); }

/* get_maximum_wincount() */
static int max_wincount(Frame *f, int height)
{
	if (f->layout != FR_COL) return height / (WM + STATUS);
	int n = 0;
	for (Frame *c = f->child; c; c = c->next) {
		if (height < WM + STATUS) break;
		height -= WM + STATUS;
		n++;
	}
	return n + height / (WM + STATUS);
}

/* win_equal_rec(), without 'winfixheight' and 'winfixwidth' (never set):
   top gets the place at[] and the size sz[] (ROWS, COLS) */
static void equal_rec(Editor *e, Win *next, int current, Frame *top, int dir, const int at[2], const int sz[2])
{
	int extra_sep = 0, totwincount = 0, next_size = 0, room = 0, has_next = 0;
	if (top->win) {
		Win *w = top->win;
		if (top->size[ROWS] != sz[ROWS] || w->y != at[ROWS] || top->size[COLS] != sz[COLS] || w->x != at[COLS])
			for (int a = ROWS; a <= COLS; a++) { *wpos(w, a) = at[a]; new_size(e, top, sz[a], 0, a); }
		return;
	}
	/* children lie along axis a; dir 'v' equalizes only heights, 'h' only widths */
	int a = top->layout == FR_ROW ? COLS : ROWS, keep = a == COLS ? 'v' : 'h';
	int pos[2] = { at[ROWS], at[COLS] }, left[2] = { sz[ROWS], sz[COLS] };
	top->size[ROWS] = sz[ROWS];
	top->size[COLS] = sz[COLS];
	if (dir != keep) {
		int n = minsize(top, NULL, a), m = minsize(top, next, a);
		if (a == COLS) {
			extra_sep = at[COLS] + sz[COLS] == e->cols;
			totwincount = (n + extra_sep) / (WM + 1);
		} else totwincount = max_wincount(top, n);
		room = sz[a] - m;
		has_next = has_win(top, next);
		int wh = p_wsz[a];
		if (room < 0) {
			next_size = wh + room;
			room = 0;
		} else if (!has_next) next_size = 0;
		else if (totwincount > 1 && (room + (totwincount - 2)) / (totwincount - 1) > wh) {
			next_size = (room + wh + (totwincount - 1) * WM + (totwincount - 1)) / totwincount;
			room -= next_size - wh;
		} else next_size = wh;
		if (has_next) totwincount--;
	}
	for (Frame *c = top->child; c; c = c->next) {
		int wincount = 1, size;
		if (!c->next) size = left[a];
		else if (dir == keep) size = c->size[a];
		else {
			int n = minsize(c, NULL, a), m = minsize(c, next, a);
			/* Neovim adds extra_sep for the last frame, which the first branch takes */
			wincount = a == COLS ? n / (WM + 1) : max_wincount(c, n);
			int hnc = has_next && has_win(c, next);
			if (hnc) wincount--;
			size = totwincount ? (wincount * room + totwincount / 2) / totwincount : room;
			if (hnc) {
				next_size -= p_wsz[a] - (m - n);
				if (a == COLS && next_size < 0) next_size = 0;
				size += next_size;
				room -= size - next_size;
			} else room -= size;
			size += n;
		}
		if (!current || dir != keep || top->parent || size != c->size[a] || has_win(c, next)) {
			int csz[2] = { left[ROWS], left[COLS] };
			csz[a] = size;
			equal_rec(e, next, current, c, dir, pos, csz);
		}
		pos[a] += size;
		left[a] -= size;
		totwincount -= wincount;
	}
}

static void win_equal(Editor *e, Win *next, int current, int dir)
{
	equal_rec(e, next ? next : e->win, current, e->layout, dir, (int[2]){ e->layout->top, 0 },
	          (int[2]){ e->layout->size[ROWS], e->cols });
}

/* ---- window list ---- */

static void win_unlink(Editor *e, Win *w)
{
	Win **p = &e->wins;
	while (*p != w) p = &(*p)->next;
	*p = w->next;
}

static void win_append(Editor *e, Win *after, Win *w)
{
	Win **p = after ? &after->next : &e->wins;
	w->next = *p;
	*p = w;
}

static Win *win_prev(Editor *e, Win *w)
{
	Win *p = e->wins;
	if (p == w) return NULL;
	while (p->next != w) p = p->next;
	return p;
}

static void frame_insert(Frame *before, Frame *f)
{
	f->next = before;
	f->prev = before->prev;
	before->prev = f;
	if (f->prev) f->prev->next = f;
	else f->parent->child = f;
}

static void frame_append(Frame *after, Frame *f)
{
	f->prev = after;
	f->next = after->next;
	after->next = f;
	if (f->next) f->next->prev = f;
}

static void frame_remove(Frame *f)
{
	if (f->prev) f->prev->next = f->next;
	else f->parent->child = f->next;
	if (f->next) f->next->prev = f->prev;
}

void layout_init(Editor *e, Win *w, int ch)
{
	Frame *f = frame_new(FR_LEAF);
	if (!f) return;
	f->win = w;
	w->frame = f;
	f->s.vline = (size_t)-1;
	f->s.bot = 1;                       /* win_init_empty(): w_botline 2 */
	e->layout = f;
	f->top = tabline_height(e);
	f->size[ROWS] = e->rows - ch - f->top;   /* win_alloc_firstwin(): ROWS_AVAIL */
	f->size[COLS] = e->cols;
	w->h = f->size[ROWS] - STATUS;
	w->w = f->size[COLS];
	w->y = f->top;
}

/* win_new_screensize(), and win_new_screen_rows() when the tabline comes
   or goes: the top frame takes the screen, the command line keeps its height */
void layout_resize(Editor *e, int rows, int cols)
{
	int ch = cmdheight(e), top = tabline_height(e);
	if (rows == e->rows && cols == e->cols && top == e->layout->top) return;
	e->rows = rows;
	e->cols = cols;
	e->layout->top = top;
	if (rows - ch - top < 2) ch = rows - top > 2 ? rows - top - 2 : 1;
	new_size(e, e->layout, rows - ch - top, 0, ROWS);
	new_size(e, e->layout, cols, 0, COLS);
	win_comp_pos(e);
}

/* ---- entering ---- */

/* win_enter_ext(): prevwin, then make w current and at least winwidth wide */
static void win_setsize(Editor *e, Win *w, int size, int a);
void win_enter(Editor *e, Win *w)
{
	if (w == e->win) return;
	e->prev = e->win;
	e->win = w;
	e->buf = w->buf;
	win_check_cursor(w);
	/* win_enter_ext()'s changed_line_abv_curs(): the view is looked at
	   again with the window's real botline (review of PR 20, 6) */
	w->frame->s.topvalid = 0;
	/* (Neovim's "else if (w_height == 0)" is dead: winheight is at least 1) */
	for (int a = ROWS; a <= COLS; a++) if (*wsize(w, a) < p_wsz[a]) win_setsize(e, w, p_wsz[a], a);
}

/* ---- splitting ---- */

/* win_init(): the new window starts as a copy of the old one */
static void win_copy_view(Win *n, const Win *o);
static void win_init(Win *n, Win *o)
{
	win_copy_view(n, o);
	n->frame->s.wrow = o->frame->s.wrow;
	n->frame->s.fraction = o->frame->s.fraction;
	n->frame->s.prev_fraction_row = o->frame->s.prev_fraction_row;
	n->frame->s.vline = (size_t)-1;   /* w_valid = 0, w_valid_cursor not set */
	n->frame->s.bot = 1;              /* win_alloc(): w_botline 2; splitkeep=cursor copies none */
	n->frame->s.wrap = n->wrap;
	n->frame->s.allwrap = o->frame->s.allwrap;   /* win_copy_options() */
}

/* what win_init() copies of the window itself */
static void win_copy_view(Win *n, const Win *o)
{
	n->buf = o->buf;
	n->alt = o->alt;
	n->line = o->line; n->col = o->col; n->want = o->want;
	n->top = o->top; n->left = o->left;
	n->wrap = o->wrap;
	n->conceal = o->conceal;   /* win_copy_options() */
	memcpy(n->jump, o->jump, (size_t)o->njump * sizeof o->jump[0]);   /* copy_jumplist() */
	n->njump = o->njump;
	n->jumpidx = o->jumpidx;
	n->pcmark = o->pcmark;                                            /* win_init() */
	n->changeidx = o->changeidx;
}

static void flatten(Editor *e, Frame *f);

/* win_split_ins(): new_wp is a window being moved (CTRL-W HJKL), else a new
   one is made; dir is the direction the moved window's frame was removed in,
   and to_flatten the frame left alone there, flattened only once the split
   cannot fail on room, so that a failed move can be undone */
static Win *split_ins(Editor *e, int size, int flags, Win *new_wp, int dir, Frame *to_flatten)
{
	Win *old = flags & WSP_TOP ? e->wins : flags & WSP_BOT ? NULL : e->win, *w = new_wp;
	if (flags & WSP_BOT) { old = e->wins; while (old->next) old = old->next; }
	int vertical = flags & WSP_VERT, toplevel = flags & (WSP_TOP | WSP_BOT);
	/* the split is on axis a; the new window's separator or status line is 1 */
	int a = vertical ? COLS : ROWS, do_equal = 0, nsz = size, old_size = *wsize(old, a);
	Frame *of = old->frame;
	int mins = minsize(toplevel ? e->layout : of, NULL, a), avail = e->layout->size[a];
	if (!toplevel)
		for (Frame *pf = of, *p = of->parent; p; pf = p, p = p->parent)
			if (p->layout == along(a))
				for (Frame *c = p->child; c; c = c->next) if (c != pf) mins += minsize(c, NULL, a);
	if (avail < WM + 1 + mins) { core_msg(e, "E36: Not enough room"); return NULL; }
	if (!nsz) nsz = old_size / 2;
	if (nsz > avail - mins - 1) nsz = avail - mins - 1;
	if (nsz < WM) nsz = WM;
	if (old_size - nsz - 1 < WM) do_equal = 1;
	if (!do_equal && size == 0 && of->parent)
		for (Frame *c = of->parent->child; c; c = c->next)
			if (c->win && c->win != old && (*wsize(c->win, a) > nsz || *wsize(c->win, a) > old_size - nsz - 1)) {
				do_equal = 1;
				break;
			}
	/* splitbelow and splitright: the new window goes after the old one */
	int after = !(flags & WSP_TOP);
	if (!new_wp) {
		if (!(w = calloc(1, sizeof *w)) || !(w->frame = frame_new(FR_LEAF))) {
			free(w);
			core_msg(e, "out of memory");
			return NULL;
		}
		w->frame->win = w;
		win_init(w, e->win);
	}
	win_append(e, after ? old : win_prev(e, old), w);
	if (to_flatten) {
		flatten(e, to_flatten);
		of = old->frame;
	}
	Frame *cur;
	int before;
	Frame *top = e->layout;
	if (toplevel) {
		if ((top->layout == FR_COL && !vertical) || (top->layout == FR_ROW && vertical)) {
			cur = top->child;
			if (flags & WSP_BOT) while (cur->next) cur = cur->next;
		} else cur = top;
		before = flags & WSP_TOP;
	} else {
		cur = of;
		before = 0;
	}
	int layout = vertical ? FR_ROW : FR_COL;
	if (!cur->parent || cur->parent->layout != layout) {
		/* cur becomes the parent of a copy of itself */
		Frame *c = frame_new(FR_LEAF);
		if (!c) { win_unlink(e, w); if (!new_wp) { free(w->frame); free(w); } core_msg(e, "out of memory"); return NULL; }
		*c = *cur;
		cur->layout = layout;
		c->parent = cur;
		c->next = c->prev = NULL;
		cur->child = c;
		cur->win = NULL;
		if (c->win) c->win->frame = c;
		else for (Frame *g = c->child; g; g = g->next) g->parent = c;
		if (cur == of) of = c;
		cur = c;
	}
	Frame *nf = w->frame;
	nf->parent = cur->parent;
	if (before) frame_insert(cur, nf);
	else frame_append(cur, nf);
	win_set_fraction(old);
	nf->s.fraction = old->frame->s.fraction;
	/* across the split (axis o) the new window takes old's place, or the
	   whole of cur at the top level */
	int o = !a;
	if (a == COLS) w->scroll = e->win->scroll;
	else nf->vsep = toplevel ? 0 : of->vsep;
	*wpos(w, o) = toplevel ? 0 : *wpos(old, o);
	win_new_size(e, w, toplevel ? cur->size[o] - edge(nf, o) : *wsize(old, o), o);
	nf->size[o] = cur->size[o];
	win_new_size(e, w, nsz, a);
	if (a == COLS) {
		nf->vsep = before ? 1 : of->vsep;
		of->vsep |= !before;
	}
	if (toplevel) {
		if (a == COLS && flags & WSP_BOT) set_vsep(e, cur, 1);
		new_size(e, cur, cur->size[a] - nsz - (a == ROWS ? STATUS : (flags & WSP_TOP) != 0), flags & WSP_TOP, a);
	} else win_new_size(e, old, old_size - (nsz + 1), a);
	if (before) { *wpos(w, a) = *wpos(old, a); *wpos(old, a) += nsz + 1; }
	else *wpos(w, a) = *wpos(old, a) + *wsize(old, a) + 1;
	fix_size(old, a);
	fix_size(w, a);
	if (toplevel) win_comp_pos(e);
	if (do_equal || dir)
		win_equal(e, w, 1, vertical ? (dir == 'v' ? 'b' : 'h') : (dir == 'h' ? 'b' : 'v'));
	int *opt = &p_wsz[a], keep = *opt;
	if (size) *opt = size;
	win_enter(e, w);
	*opt = keep;
	return w;
}

int core_split(Editor *e, int vertical, int size)
{
	return split_ins(e, size, vertical ? WSP_VERT : 0, NULL, 0, NULL) ? 0 : -1;
}

/* ---- closing ---- */

/* frame_flatten(): a parent left with one child becomes that child */
static void flatten(Editor *e, Frame *f)
{
	if (f->next || f->prev) return;
	Frame *p = f->parent;
	p->layout = f->layout;
	p->child = f->child;
	for (Frame *c = f->child; c; c = c->next) c->parent = p;
	p->win = f->win;
	if (f->win) {
		p->win->frame = p;
		p->vsep = f->vsep;
		p->s = f->s;
	}
	free(f);
	Frame *g = p->parent;
	if (g && g->layout == p->layout) {
		/* p's children join g in p's place */
		if (g->child == p) g->child = p->child;
		p->child->prev = p->prev;
		if (p->prev) p->prev->next = p->child;
		for (Frame *c = p->child;; c = c->next) {
			c->parent = g;
			if (!c->next) {
				c->next = p->next;
				if (p->next) p->next->prev = c;
				break;
			}
		}
		free(p);
	}
	(void)e;
}

/* win_altframe(): the frame that gets the closed window's space */
static Frame *altframe(Frame *f)
{
	if (!f->prev) return f->next;
	if (!f->next) return f->prev;
	return f->prev;               /* splitbelow and splitright */
}

/* winframe_remove(); returns the window that got the space and sets *dir */
static Win *frame_take_out(Editor *e, Win *w, int *dir, Frame **alt)
{
	Frame *f = w->frame, *a = altframe(f);
	Win *top = frame2win(f->parent);
	int row = top->y, col = top->x;
	*dir = f->parent->layout == FR_COL ? 'v' : 'h';
	if (!f->vsep && f->parent->layout == FR_ROW && f->prev) set_vsep(e, f->prev, 0);
	frame_remove(f);
	int x = *dir == 'v' ? ROWS : COLS;
	new_size(e, a, a->size[x] + f->size[x], a == f->next, x);
	if (a != f->prev) comp_pos(f->parent, &row, &col);
	*alt = a;
	return frame2win(a);
}

static int one_window(Editor *e) { return !e->wins->next; }
static Tab *alt_tab(Editor *e);
static void tab_close_last(Editor *e, Tab *to, int enter);

static void win_forget(Editor *e, Win *w)
{
	/* win_free(): the entry loses its window; of two such entries only the
	   first is ever used, so the later one goes */
	for (int k = 0; k < 2; k++)
	for (Buf *b = k ? buf_unlisted() : e->bufs; b; b = b->next) {
		WinInfo **mine = NULL, **null = NULL, **p;
		size_t k = 0, km = 0, kn = 0;
		for (p = &b->wi; *p; p = &(*p)->next, k++)
			if ((*p)->win == w) { mine = p; km = k; }
			else if (!(*p)->win) { null = p; kn = k; }
		if (!mine) continue;
		(*mine)->win = NULL;
		if (null) {
			WinInfo **d = km > kn ? mine : null, *x = *d;
			*d = x->next;
			free(x);
		}
	}
	if (e->prev == w) e->prev = NULL;
	free(w->frame);
	free(w);
}

static int quit_check(Editor *e, int force)
{
	if (force) return 0;
	if (core_modified(e->buf)) {
		core_msg(e, "E37: No write since last change (add ! to override)");
		return -1;
	}
	for (int k = 0; k < 2; k++)
		for (Buf *b = k ? buf_unlisted() : e->bufs; b; b = b->next)
			if (core_modified(b)) {
				core_msg(e, "E162: No write since last change for buffer \"%s\"", b->path ? b->path : "[No Name]");
				return -1;
			}
	return 0;
}

/* win_close(): w goes; the window that gets its space becomes current if w
   was.  The caller makes sure w is not the last window. */
void win_close(Editor *e, Win *w)
{
	Frame *parent = w->frame->parent, *alt;
	int dir, cur = w == e->win;
	core_wininfo_set(w->buf, w, 1);    /* close_buffer() remembers the cursor */
	Win *to = frame_take_out(e, w, &dir, &alt);
	Frame *f = w->frame;
	win_unlink(e, w);
	flatten(e, alt);
	w->frame = f;
	win_forget(e, w);
	if (cur) {
		e->win = to;
		e->buf = to->buf;
		win_check_cursor(to);
	}
	/* equalalways: only the closed window's frame if it holds the current window */
	win_equal(e, e->win, e->win->frame->parent == parent, dir);
	if (cur) {
		Win *pw = e->prev;
		e->win = NULL;
		win_enter(e, to);
		e->prev = pw;
	}
}

int core_close(Editor *e, int force)
{
	if (one_window(e) && e->tabs->next) { tab_close_last(e, alt_tab(e), 1); return 0; }
	if (one_window(e)) {
		if (quit_check(e, force)) return -1;
		e->quit = 1;
		return 0;
	}
	win_close(e, e->win);
	return 0;
}

/* close_others(): every other window, from the first */
int core_only(Editor *e)
{
	for (Win *w = e->wins, *nx; w; w = nx) {
		nx = w->next;
		if (w != e->win) win_close(e, w);
	}
	return 0;
}

/* ---- moving between windows ---- */

/* win_vert_neighbor(), win_horz_neighbor(): the window count frames away,
   choosing among split frames by the cursor's screen position */
static Win *neighbor(Editor *e, Win *w, int vert, int back, long count)
{
	Frame *found = w->frame;
	int wrow, wcol;
	win_cursor_pos(e, w, &wrow, &wcol);
	while (count--) {
		Frame *f = found, *nf;
		for (;;) {
			if (f == e->layout) return found->win;
			nf = back ? f->prev : f->next;
			if (f->parent->layout == (vert ? FR_COL : FR_ROW) && nf) break;
			f = f->parent;
		}
		for (;;) {
			if (nf->win) { found = nf; break; }
			f = nf->child;
			int o = vert ? COLS : ROWS;
			if (nf->layout == along(o))
				while (f->next && *wpos(frame2win(f), o) + f->size[o] <= *wpos(w, o) + (vert ? wcol : wrow))
					f = f->next;
			if (nf->layout == (vert ? FR_COL : FR_ROW) && back) while (f->next) f = f->next;
			nf = f;
		}
	}
	return found->win;
}

int core_win_goto(Editor *e, int how, long count)
{
	Win *w = NULL;
	switch (how) {
	case 'w': case 'W':
		if (one_window(e) && count != 1) return -1;
		if (count) for (w = e->wins; --count > 0 && w->next;) w = w->next;
		else if (how == 'W') { if (!(w = win_prev(e, e->win))) for (w = e->wins; w->next;) w = w->next; }
		else if (!(w = e->win->next)) w = e->wins;
		break;
	case 'h': case 'l': w = neighbor(e, e->win, 0, how == 'h', count ? count : 1); break;
	case 'k': case 'j': w = neighbor(e, e->win, 1, how == 'k', count ? count : 1); break;
	case 't': w = e->wins; break;
	case 'b': for (w = e->wins; w->next;) w = w->next; break;
	case 'p': if (!(w = e->prev)) return -1; break;
	}
	if (!w) return -1;
	win_enter(e, w);
	return 0;
}

/* win_exchange() */
static void exchange(Editor *e, long count)
{
	Win *cw = e->win;
	Frame *f;
	if (count) for (f = cw->frame->parent->child; f && --count > 0;) f = f->next;
	else f = cw->frame->next ? cw->frame->next : cw->frame->prev;
	if (!f || !f->win || f->win == cw) return;
	Win *w = f->win, *w2 = win_prev(e, cw);
	Frame *f2 = cw->frame->prev;
	if (win_prev(e, w) != cw) {
		win_unlink(e, cw);
		frame_remove(cw->frame);
		win_append(e, win_prev(e, w), cw);
		frame_insert(f, cw->frame);
	}
	if (w != w2) {
		win_unlink(e, w);
		frame_remove(w->frame);
		win_append(e, w2, w);
		if (!f2) frame_insert(w->frame->parent->child, w->frame);
		else frame_append(f2, w->frame);
	}
	int t = cw->frame->vsep;
	cw->frame->vsep = w->frame->vsep;
	w->frame->vsep = t;
	for (int a = ROWS; a <= COLS; a++) { fix_size(cw, a); fix_size(w, a); }
	win_comp_pos(e);
	win_enter(e, w);
}

/* win_rotate() */
static int rotate(Editor *e, int up, long count)
{
	for (Frame *f = e->win->frame->parent->child; f; f = f->next)
		if (!f->win) { core_msg(e, "E443: Cannot rotate when another window is split"); return -1; }
	while (count--) {
		Frame *f;
		Win *w1, *w2;
		if (up) {                  /* the first becomes the last */
			f = e->win->frame->parent->child;
			w1 = f->win;
			win_unlink(e, w1);
			frame_remove(f);
			Frame *l = f->parent->child;
			while (l->next) l = l->next;
			win_append(e, l->win, w1);
			frame_append(l, f);
			w2 = l->win;
		} else {                   /* the last becomes the first */
			for (f = e->win->frame; f->next;) f = f->next;
			w1 = f->win;
			w2 = win_prev(e, w1);
			win_unlink(e, w1);
			frame_remove(f);
			win_append(e, win_prev(e, f->parent->child->win), w1);
			frame_insert(f->parent->child, f);
		}
		int t = w2->frame->vsep;
		w2->frame->vsep = w1->frame->vsep;
		w1->frame->vsep = t;
		for (int a = ROWS; a <= COLS; a++) { fix_size(w1, a); fix_size(w2, a); }
		win_comp_pos(e);
	}
	return 0;
}

/* win_splitmove(): CTRL-W H J K L; a count is the new size */
static int splitmove(Editor *e, int flags, int size)
{
	Win *w = e->win, *prev = win_prev(e, w);
	int h = w->h, dir;
	Frame *alt, *f = w->frame;
	frame_take_out(e, w, &dir, &alt);
	win_unlink(e, w);
	win_comp_pos(e);
	/* curwin stays w, so win_enter() does nothing and 'winheight' /
	   'winwidth' do not apply (measured: 7 CTRL-W K gives the equalized
	   6 lines, in 15 rows) */
	if (!split_ins(e, size, flags, w, dir, alt)) {
		/* winframe_restore(): the frame back where it was, the room back */
		if (f->prev) frame_append(f->prev, f);
		else frame_insert(f->next, f);
		if (!f->vsep && f->parent->layout == FR_ROW && f->prev) set_vsep(e, f->prev, 1);
		int a = dir == 'v' ? ROWS : COLS;
		new_size(e, alt, alt->size[a] - f->size[a], alt == f->next, a);
		win_comp_pos(e);
		win_append(e, prev, w);
		return -1;
	}
	if (!size && !(flags & WSP_VERT)) {
		win_setsize(e, w, h, ROWS);
		win_equal(e, e->win, e->win == w, 'v');
	}
	return 0;
}

int core_win_move(Editor *e, int how, long count)
{
	if (one_window(e)) return 0;
	switch (how) {
	case 'x': exchange(e, count); return 0;
	case 'r': case 'R': return rotate(e, how == 'R', count ? count : 1);
	case 'H': return splitmove(e, WSP_VERT | WSP_TOP, (int)count);
	case 'L': return splitmove(e, WSP_VERT | WSP_BOT, (int)count);
	case 'K': return splitmove(e, WSP_TOP, (int)count);
	case 'J': return splitmove(e, WSP_BOT, (int)count);
	}
	return -1;
}

/* ---- sizes ---- */

/* frame_new_height() on the top frame with set_ch: the command line takes
   what the windows give up (command_height()) */
static void top_new_height(Editor *e, int height)
{
	Frame *t = e->layout;
	int ch = cmdheight(e), nch = ch + t->size[ROWS] - height;
	if (nch < 1) nch = 1;
	if (nch != ch) {
		/* command_height(): the last full-width frame gives or takes the lines */
		Frame *f = e->wins->frame;
		Win *l = e->wins;
		while (l->next) l = l->next;
		f = l->frame;
		while (f->size[COLS] != e->cols && f->parent) f = f->parent;
		int old = ch;
		while (nch > old && f) {
			int h = nch - old < f->size[ROWS] - minsize(f, NULL, ROWS) ? nch - old : f->size[ROWS] - minsize(f, NULL, ROWS);
			new_size(e, f, f->size[ROWS] - h, 0, ROWS);
			for (Frame *p = f->parent; p; p = p->parent) p->size[ROWS] -= h;
			old += h;
			f = f->prev;
		}
		if (nch < old && f) {
			new_size(e, f, f->size[ROWS] + old - nch, 0, ROWS);
			for (Frame *p = f->parent; p; p = p->parent) p->size[ROWS] += old - nch;
		}
		ch = nch < old ? nch : old;   /* E36 keeps what could be taken */
		/* setting 'cmdheight' sets w_set_curswant, as for 'wrap' (measured:
		   5l j CTRL-W - k, review of PR 20, 2) */
		if (e->win->want != WANT_EOL) core_set_want(e);
	}
	if (height > e->rows - ch - e->layout->top) height = e->rows - ch - e->layout->top;   /* ROWS_AVAIL */
	new_size(e, t, height, 0, ROWS);
}

/* frame_setheight(), frame_setwidth(): only a height can take lines from
   the command line */
static void setsize(Editor *e, Frame *cur, int size, int a)
{
	if (cur->size[a] == size) return;
	if (!cur->parent) { if (a == ROWS && size > 0) top_new_height(e, size); return; }
	if (cur->parent->layout != along(a)) {
		int m = minsize(cur->parent, NULL, a);
		setsize(e, cur->parent, size > m ? size : m, a);
		return;
	}
	/* a frame as tall as ROWS_AVAIL or as wide as Columns has no parent to grow */
	int room = 0, room_cmdline = 0, full = a == ROWS ? e->cols : e->layout->size[ROWS];
	for (int run = 1; run <= 2; run++) {
		room = 0;
		for (Frame *f = cur->parent->child; f; f = f->next) {
			room += f->size[a];
			if (f != cur) room -= minsize(f, NULL, a);
		}
		if (a == ROWS && cur->size[COLS] == e->cols) {
			Win *l = e->wins;
			while (l->next) l = l->next;
			room_cmdline = e->rows - cmdheight(e) - (l->y + l->h + STATUS);
			if (room_cmdline < 0) room_cmdline = 0;
		} else room_cmdline = 0;
		if (size <= room + room_cmdline) break;
		if (run == 2 || cur->size[!a] >= full) { size = room + room_cmdline; break; }
		setsize(e, cur->parent, size + minsize(cur->parent, NULL, a) - WM - 1, a);
	}
	int take = size - cur->size[a];
	if (take > 0 && room_cmdline > 0) {
		if (room_cmdline > take) room_cmdline = take;
		take -= room_cmdline;
		e->layout->size[ROWS] += room_cmdline;
	}
	new_size(e, cur, size, 0, a);
	for (int run = 0; run < 2; run++)
		for (Frame *f = run ? cur->prev : cur->next; f && take; f = run ? f->prev : f->next) {
			int m = minsize(f, NULL, a);
			if (f->size[a] - take < m) { take -= f->size[a] - m; new_size(e, f, m, 0, a); }
			else { new_size(e, f, f->size[a] - take, 0, a); take = 0; }
		}
}

/* win_setheight_win(), win_setwidth_win(): every caller passes curwin */
static void win_setsize(Editor *e, Win *w, int size, int a)
{
	if (size < WM) size = WM;
	setsize(e, w->frame, size + edge(w->frame, a), a);
	win_comp_pos(e);
}

void core_resize(Editor *e, int vertical, long n, int set)
{
	Win *w = e->win;
	if (n > 100000) n = 100000;
	if (n < -100000) n = -100000;
	int a = vertical ? COLS : ROWS;
	win_setsize(e, w, set ? (n ? (int)n : a == COLS ? e->cols : e->rows - 1) : *wsize(w, a) + (int)n, a);
}

void core_equal(Editor *e) { win_equal(e, NULL, 0, 'b'); }

/* ---- tab pages (window.c), each in its own OS window (Emacs's frames) ---- */

static int tab_ids;               /* Tab.id: never reused, so the backend's handle stays unique */

Win *tab_firstwin(const Editor *e, const Tab *t) { return t == e->tab ? e->wins : t->wins; }

/* leave_tabpage() and use_tabpage(), without what goto_tabpage_tp() adds:
   drawing another tab page and closing its windows look at it this way */
void tab_use(Editor *e, Tab *t)
{
	Tab *o = e->tab;
	o->win = e->win; o->prev = e->prev; o->wins = e->wins; o->layout = e->layout;
	o->rows = e->rows; o->cols = e->cols;
	e->tab = t;
	e->win = t->win; e->prev = t->prev; e->wins = t->wins; e->layout = t->layout;
	e->rows = t->rows; e->cols = t->cols;
	e->buf = e->win->buf;
}

int tab_first(Editor *e)
{
	tab_ids = 1;
	if (!(e->tab = e->tabs = calloc(1, sizeof *e->tab))) return -1;
	e->tab->key = 'a';
	return 0;
}

/* The first letter no tab page has, f, m, n and o being SPC 5's own keys;
   0 when all 22 are taken */
static char free_key(const Editor *e)
{
	for (const char *c = "abcdeghijklpqrstuvwxyz"; *c; c++) {
		const Tab *t = e->tabs;
		while (t && t->key != *c) t = t->next;
		if (!t) return *c;
	}
	return 0;
}

void tab_free_all(Editor *e)
{
	for (Tab *t = e->tabs, *nx; t; t = nx) {
		nx = t->next;
		if (t != e->tab) {
			for (Win *w = t->wins, *wn; w; w = wn) { wn = w->next; free(w); }
			layout_free(t->layout);
		}
		free(t);
	}
}

/* enter_tabpage(): the tab page's window as win_enter_ext() with
   WEE_CURWIN_INVALID, its previous window kept */
void core_tab_enter(Editor *e, Tab *t)
{
	int rows = e->rows, cols = e->cols;
	if (t == e->tab) return;
	tab_use(e, t);
	Win *w = e->win, *pw = e->prev;
	e->win = NULL;
	win_enter(e, w);
	e->prev = pw;
	/* then the terminal's one screen keeps its size, and the tabline may
	   have come or gone (win_new_screen_rows(), win_new_screen_cols()) */
	if (e->tabline) layout_resize(e, rows, cols);
}

/* alt_tabpage(): the next tab page, or the one before the last */
static Tab *alt_tab(Editor *e)
{
	if (e->tab->next) return e->tab->next;
	Tab *t = e->tabs;
	while (t->next != e->tab) t = t->next;
	return t;
}

/* close_last_window_tabpage(): the current tab page's only window and the
   tab page go; to becomes current, entered (goto_tabpage_tp()) or, back
   from closing another tab page, as it was */
static void tab_close_last(Editor *e, Tab *to, int enter)
{
	Tab *t = e->tab, **p = &e->tabs;
	Win *w = e->win;
	core_wininfo_set(w->buf, w, 1);    /* close_buffer() remembers the cursor */
	if (enter) core_tab_enter(e, to);
	else tab_use(e, to);
	while (*p != t) p = &(*p)->next;
	*p = t->next;
	win_forget(e, w);                  /* its frame is the tab page's layout */
	free(t);
	layout_resize(e, e->rows, e->cols);   /* win_close_othertab(): the tabline may go */
}

/* win_new_tabpage() after the current one: one window copied from the
   current one, as win_alloc_firstwin(oldwin) */
static int tab_new(Editor *e)
{
	Tab *t = calloc(1, sizeof *t);
	Win *w = calloc(1, sizeof *w), *o = e->win;
	int ch = cmdheight(e);
	if (!t || !w) { free(t); free(w); core_msg(e, "out of memory"); return -1; }
	t->id = tab_ids++;
	t->key = free_key(e);
	t->next = e->tab->next;
	e->tab->next = t;
	t->win = t->wins = w;
	t->rows = e->rows;
	t->cols = e->cols;
	tab_use(e, t);
	layout_init(e, w, ch);
	if (!e->layout) { core_msg(e, "out of memory"); return -1; }
	win_init(w, o);
	/* win_alloc_firstwin() sizes the window without a status line, and
	   last_status() then takes one through win_new_height(): the cursor
	   keeps its fraction of the window (measured: :tab split of a narrow
	   window scrolls as nvim's does).  Known difference: when the command
	   line leaves the window no room for a status line, Neovim gives E36
	   and draws none; here it keeps one (core_screens seeds 14 and 16) */
	e->buf = w->buf;
	w->h += STATUS;
	win_new_height(e, w, w->h - STATUS);
	/* curwin is set, not entered: no win_enter_ext(), so no 'winheight' */
	e->prev = NULL;
	return 0;
}

/* ex_splitview() gives the window left the new buffer as its alternate
   only when win_valid(), which looks in the current tab page: never here */
int core_tabnew(Editor *e, const char *path)
{
	return tab_new(e) ? -1 : path ? edit_split(e, path) : 0;
}

int core_tabclose(Editor *e, Tab *t)
{
	Tab *cur = e->tab;
	if (!e->tabs->next) { core_msg(e, "E784: Cannot close last tab page"); return -1; }
	if (!t) t = cur;
	/* tabpage_close(): the other windows first, then the last one */
	if (t != cur) tab_use(e, t);
	core_only(e);
	tab_close_last(e, t == cur ? alt_tab(e) : cur, t == cur);
	return 0;
}

int core_tabonly(Editor *e)
{
	while (e->tabs->next) core_tabclose(e, e->tabs == e->tab ? e->tabs->next : e->tabs);
	return 0;
}

int core_win_to_tab(Editor *e, int n)
{
	Win *wp = e->win;
	Tab *old = e->tab, *to = NULL;
	if (!n) {
		/* do_window() 'T': a new tab page with a copy, then the window goes */
		if (one_window(e)) { core_msg(e, "Already only one window"); return 0; }
		if (tab_new(e)) return -1;
		to = e->tab;
	} else {
		/* SPC 5 m {n} (DESIGN.md, New approaches): a split in tab page n
		   takes the window's view, then the window goes */
		to = e->tabs;
		while (to && --n > 0) to = to->next;
		if (!to || to == old) return -1;
		core_tab_enter(e, to);
		if (core_split(e, 0, 0)) { core_tab_enter(e, old); return -1; }
		win_copy_view(e->win, wp);
		e->win->frame->s.vline = (size_t)-1;
		e->buf = wp->buf;
	}
	tab_use(e, old);
	if (one_window(e)) tab_close_last(e, to, 1);
	else { win_close(e, wp); core_tab_enter(e, to); }
	return 0;
}

/* close_windows()'s other tab pages: their windows on b close, and a tab
   page left without one closes */
void close_othertabs(Editor *e, Buf *b)
{
	Tab *cur = e->tab;
	for (Tab *t = e->tabs, *nx; t; t = nx) {
		nx = t->next;
		if (t == cur) continue;
		tab_use(e, t);
		for (Win *w = e->wins, *wn; w; w = wn) {
			wn = w->next;
			if (w->buf != b) continue;
			if (one_window(e)) { tab_close_last(e, cur, 0); break; }
			win_close(e, w);
		}
		if (e->tab != cur) tab_use(e, cur);
	}
}
