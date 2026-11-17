/* Windows and splits: Neovim's frame tree (window.c) with laststatus=2
   (every window has a status line), equalalways, splitbelow, splitright,
   winheight 1, winminheight 1, winwidth 20, winminwidth 1. */
#include <stdlib.h>
#include <string.h>
#include "core_int.h"

enum { WMH = 1, WMW = 1, STATUS = 1 };
/* 'winheight' and 'winwidth': a split with a size sets them while it enters
   the new window, as win_split_ins() does */
static int p_wh = 1, p_wiw = 20;
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

static int cmdheight(Editor *e) { return e->rows - e->layout->height; }

/* frame_minheight(), frame_minwidth(): next_curwin gets winheight and
   winwidth instead of the minimum */
static int minheight(Frame *f, Win *next)
{
	if (f->win) return (f->win == next ? p_wh : WMH) + STATUS;
	int m = 0;
	for (Frame *c = f->child; c; c = c->next) {
		int n = minheight(c, next);
		if (f->layout == FR_ROW) { if (n > m) m = n; }
		else m += n;
	}
	return m;
}

static int minwidth(Frame *f, Win *next)
{
	if (f->win) return (f->win == next ? p_wiw : WMW) + f->vsep;
	int m = 0;
	for (Frame *c = f->child; c; c = c->next) {
		int n = minwidth(c, next);
		if (f->layout == FR_COL) { if (n > m) m = n; }
		else m += n;
	}
	return m;
}

static void fix_height(Win *w) { w->frame->height = w->h + STATUS; }
static void fix_width(Win *w) { w->frame->width = w->w + w->frame->vsep; }

/* frame_new_height() */
static void new_height(Editor *e, Frame *f, int height, int topfirst)
{
	if (f->win) win_new_height(e, f->win, height - STATUS);
	else if (f->layout == FR_ROW) {
		Frame *c;
		do {
			for (c = f->child; c; c = c->next) {
				new_height(e, c, height, topfirst);
				if (c->height > height) { height = c->height; break; }
			}
		} while (c);
	} else {
		Frame *c = f->child;
		if (!topfirst) while (c->next) c = c->next;
		int extra = height - f->height;
		if (extra < 0) {
			while (c) {
				int h = minheight(c, NULL);
				if (c->height + extra < h) {
					extra += c->height - h;
					new_height(e, c, h, topfirst);
				} else {
					new_height(e, c, c->height + extra, topfirst);
					break;
				}
				c = topfirst ? c->next : c->prev;
				if (!c) height -= extra;
			}
		} else if (extra > 0) new_height(e, c, c->height + extra, topfirst);
	}
	f->height = height;
}

/* frame_new_width() */
static void new_width(Editor *e, Frame *f, int width, int leftfirst)
{
	if (f->win) {
		Frame *p = f;
		while (p->parent && !(p->parent->layout == FR_ROW && p->next)) p = p->parent;
		if (!p->parent) f->vsep = 0;
		win_new_width(e, f->win, width - f->vsep);
	} else if (f->layout == FR_COL) {
		Frame *c;
		do {
			for (c = f->child; c; c = c->next) {
				new_width(e, c, width, leftfirst);
				if (c->width > width) { width = c->width; break; }
			}
		} while (c);
	} else {
		Frame *c = f->child;
		if (!leftfirst) while (c->next) c = c->next;
		int extra = width - f->width;
		if (extra < 0) {
			while (c) {
				int w = minwidth(c, NULL);
				if (c->width + extra < w) {
					extra += c->width - w;
					new_width(e, c, w, leftfirst);
				} else {
					new_width(e, c, c->width + extra, leftfirst);
					break;
				}
				c = leftfirst ? c->next : c->prev;
				if (!c) width -= extra;
			}
		} else if (extra > 0) new_width(e, c, c->width + extra, leftfirst);
	}
	f->width = width;
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
		*row += w->h + STATUS > f->height ? f->height : w->h + STATUS;
		*col += w->w + f->vsep;
		return;
	}
	int r0 = *row, c0 = *col;
	for (Frame *c = f->child; c; c = c->next) {
		if (f->layout == FR_ROW) *row = r0;
		else *col = c0;
		comp_pos(c, row, col);
	}
}

static void win_comp_pos(Editor *e) { int r = 0, c = 0; comp_pos(e->layout, &r, &c); }

/* get_maximum_wincount() */
static int max_wincount(Frame *f, int height)
{
	if (f->layout != FR_COL) return height / (WMH + STATUS);
	int n = 0;
	for (Frame *c = f->child; c; c = c->next) {
		if (height < WMH + STATUS) break;
		height -= WMH + STATUS;
		n++;
	}
	return n + height / (WMH + STATUS);
}

/* win_equal_rec(), without 'winfixheight' and 'winfixwidth' (never set) */
static void equal_rec(Editor *e, Win *next, int current, Frame *top, int dir,
                      int col, int row, int width, int height)
{
	int extra_sep = 0, totwincount = 0, next_size = 0, room = 0, has_next = 0;
	if (top->win) {
		if (top->height != height || top->win->y != row || top->width != width || top->win->x != col) {
			top->win->y = row;
			new_height(e, top, height, 0);
			top->win->x = col;
			new_width(e, top, width, 0);
		}
		return;
	}
	int rowl = top->layout == FR_ROW;
	top->width = width;
	top->height = height;
	if (dir != (rowl ? 'v' : 'h')) {
		int n, m;
		if (rowl) {
			n = minwidth(top, NULL);
			extra_sep = col + width == e->cols;
			totwincount = (n + extra_sep) / (WMW + 1);
			m = minwidth(top, next);
			room = width - m;
		} else {
			n = minheight(top, NULL);
			totwincount = max_wincount(top, n);
			m = minheight(top, next);
			room = height - m;
		}
		has_next = has_win(top, next);
		int wh = rowl ? p_wiw : p_wh, wm = rowl ? WMW : WMH;
		if (room < 0) {
			next_size = wh + room;
			room = 0;
		} else if (!has_next) next_size = 0;
		else if (totwincount > 1 && (room + (totwincount - 2)) / (totwincount - 1) > wh) {
			next_size = (room + wh + (totwincount - 1) * wm + (totwincount - 1)) / totwincount;
			room -= next_size - wh;
		} else next_size = wh;
		if (has_next) totwincount--;
	}
	for (Frame *c = top->child; c; c = c->next) {
		int wincount = 1, size;
		if (!c->next) size = rowl ? width : height;
		else if (dir == (rowl ? 'v' : 'h')) size = rowl ? c->width : c->height;
		else {
			int n = rowl ? minwidth(c, NULL) : minheight(c, NULL);
			int m = rowl ? minwidth(c, next) : minheight(c, next);
			wincount = rowl ? (n + (c->next ? 0 : extra_sep)) / (WMW + 1) : max_wincount(c, n);
			int hnc = has_next && has_win(c, next);
			if (hnc) wincount--;
			size = totwincount ? (wincount * room + totwincount / 2) / totwincount : room;
			if (hnc) {
				next_size -= (rowl ? p_wiw : p_wh) - (m - n);
				if (rowl && next_size < 0) next_size = 0;
				size += next_size;
				room -= size - next_size;
			} else room -= size;
			size += n;
		}
		if (!current || dir != (rowl ? 'v' : 'h') || top->parent
		    || size != (rowl ? c->width : c->height) || has_win(c, next)) {
			if (rowl) equal_rec(e, next, current, c, dir, col, row, size, height);
			else equal_rec(e, next, current, c, dir, col, row, width, size);
		}
		if (rowl) { col += size; width -= size; }
		else { row += size; height -= size; }
		totwincount -= wincount;
	}
}

static void win_equal(Editor *e, Win *next, int current, int dir)
{
	equal_rec(e, next ? next : e->win, current, e->layout, dir, 0, 0, e->cols, e->layout->height);
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

void layout_init(Editor *e, Win *w)
{
	Frame *f = frame_new(FR_LEAF);
	if (!f) return;
	f->win = w;
	w->frame = f;
	f->s.vline = (size_t)-1;
	f->s.bot = 1;                       /* win_init_empty(): w_botline 2 */
	e->layout = f;
	f->height = e->rows - 1;            /* cmdheight 1 */
	f->width = e->cols;
	w->h = f->height - STATUS;
	w->w = f->width;
}

/* win_new_screensize(): the top frame takes the screen, the command line keeps its height */
void layout_resize(Editor *e, int rows, int cols)
{
	int ch = cmdheight(e);
	if (rows == e->rows && cols == e->cols) return;
	e->rows = rows;
	e->cols = cols;
	if (rows - ch < 2) ch = rows > 2 ? rows - 2 : 1;
	new_height(e, e->layout, rows - ch, 0);
	new_width(e, e->layout, cols, 0);
	win_comp_pos(e);
}

/* ---- entering ---- */

/* win_enter_ext(): prevwin, then make w current and at least winwidth wide */
static void win_setwidth(Editor *e, Win *w, int width);
static void win_setheight(Editor *e, Win *w, int height);
void win_enter(Editor *e, Win *w)
{
	if (w == e->win) return;
	e->prev = e->win;
	e->win = w;
	e->buf = w->buf;
	win_check_cursor(w);
	if (w->h < p_wh) win_setheight(e, w, p_wh);
	else if (w->h == 0) win_setheight(e, w, 1);
	if (w->w < p_wiw) win_setwidth(e, w, p_wiw);
}

/* ---- splitting ---- */

/* win_init(): the new window starts as a copy of the old one */
static void win_init(Win *n, Win *o)
{
	n->buf = o->buf;
	n->alt = o->alt;
	n->line = o->line; n->col = o->col; n->want = o->want;
	n->top = o->top; n->left = o->left;
	n->wrap = o->wrap;
	n->frame->s.wrow = o->frame->s.wrow;
	n->frame->s.fraction = o->frame->s.fraction;
	n->frame->s.prev_fraction_row = o->frame->s.prev_fraction_row;
	n->frame->s.vline = (size_t)-1;   /* w_valid = 0, w_valid_cursor not set */
	n->frame->s.bot = o->frame->s.bot;
	n->frame->s.wrap = n->wrap;
}

/* win_split_ins(): new_wp is a window being moved (CTRL-W HJKL), else a new
   one is made; dir is the direction the moved window's frame was removed in */
static Win *split_ins(Editor *e, int size, int flags, Win *new_wp, int dir)
{
	Win *old = flags & WSP_TOP ? e->wins : flags & WSP_BOT ? NULL : e->win, *w = new_wp;
	if (flags & WSP_BOT) { old = e->wins; while (old->next) old = old->next; }
	int vertical = flags & WSP_VERT, toplevel = flags & (WSP_TOP | WSP_BOT);
	int do_equal = 0, old_h = 0, new_size = size;
	Frame *of = old->frame;
	if (vertical) {
		int need = WMW + 1, minw, avail;
		if (toplevel) { minw = minwidth(e->layout, NULL); avail = e->layout->width; }
		else {
			minw = minwidth(of, NULL);
			for (Frame *pf = of, *p = of->parent; p; pf = p, p = p->parent)
				if (p->layout == FR_ROW)
					for (Frame *c = p->child; c; c = c->next) if (c != pf) minw += minwidth(c, NULL);
			avail = e->layout->width;
		}
		if (avail < need + minw) { core_msg(e, "E36: Not enough room"); return NULL; }
		if (!new_size) new_size = old->w / 2;
		if (new_size > avail - minw - 1) new_size = avail - minw - 1;
		if (new_size < WMW) new_size = WMW;
		if (old->w - new_size - 1 < WMW) do_equal = 1;
		if (!do_equal && size == 0 && of->parent)
			for (Frame *c = of->parent->child; c; c = c->next)
				if (c->win && c->win != old && (c->win->w > new_size || c->win->w > old->w - new_size - 1)) {
					do_equal = 1;
					break;
				}
	} else {
		int need = WMH + STATUS, minh, avail;
		if (toplevel) { minh = minheight(e->layout, NULL); avail = e->layout->height; }
		else {
			minh = minheight(of, NULL);
			for (Frame *pf = of, *p = of->parent; p; pf = p, p = p->parent)
				if (p->layout == FR_COL)
					for (Frame *c = p->child; c; c = c->next) if (c != pf) minh += minheight(c, NULL);
			avail = e->layout->height;
		}
		if (avail < need + minh) { core_msg(e, "E36: Not enough room"); return NULL; }
		old_h = old->h;
		if (!new_size) new_size = old_h / 2;
		if (new_size > avail - minh - STATUS) new_size = avail - minh - STATUS;
		if (new_size < WMH) new_size = WMH;
		if (old_h - new_size - STATUS < WMH) do_equal = 1;
		if (!do_equal && size == 0 && of->parent)
			for (Frame *c = of->parent->child; c; c = c->next)
				if (c->win && c->win != old && (c->win->h > new_size || c->win->h > old_h - new_size - STATUS)) {
					do_equal = 1;
					break;
				}
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
	if (vertical) {
		w->scroll = e->win->scroll;
		if (toplevel) {
			w->y = 0;
			win_new_height(e, w, cur->height - STATUS);
		} else {
			w->y = old->y;
			win_new_height(e, w, old->h);
		}
		nf->height = cur->height;
		win_new_width(e, w, new_size);
		if (before) nf->vsep = 1;
		else {
			nf->vsep = of->vsep;
			of->vsep = 1;
		}
		if (toplevel) {
			if (flags & WSP_BOT) set_vsep(e, cur, 1);
			new_width(e, cur, cur->width - (new_size + ((flags & WSP_TOP) != 0)), flags & WSP_TOP);
		} else win_new_width(e, old, old->w - (new_size + 1));
		if (before) { w->x = old->x; old->x += new_size + 1; }
		else w->x = old->x + old->w + 1;
		fix_width(old);
		fix_width(w);
	} else {
		if (toplevel) {
			w->x = 0;
			win_new_width(e, w, e->cols);
			nf->vsep = 0;
		} else {
			w->x = old->x;
			win_new_width(e, w, old->w);
			nf->vsep = of->vsep;
		}
		nf->width = cur->width;
		win_new_height(e, w, new_size);
		if (toplevel) new_height(e, cur, cur->height - new_size - STATUS, flags & WSP_TOP);
		else win_new_height(e, old, old_h - (new_size + STATUS));
		if (before) { w->y = old->y; old->y += w->h + STATUS; }
		else w->y = old->y + old->h + STATUS;
		fix_height(w);
		fix_height(old);
	}
	if (toplevel) win_comp_pos(e);
	if (do_equal || dir)
		win_equal(e, w, 1, vertical ? (dir == 'v' ? 'b' : 'h') : (dir == 'h' ? 'b' : 'v'));
	int *opt = vertical ? &p_wiw : &p_wh, keep = *opt;
	if (size) *opt = size;
	win_enter(e, w);
	*opt = keep;
	return w;
}

int core_split(Editor *e, int vertical, int size)
{
	return split_ins(e, size, vertical ? WSP_VERT : 0, NULL, 0) ? 0 : -1;
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
	if (*dir == 'v') new_height(e, a, a->height + f->height, a == f->next);
	else new_width(e, a, a->width + f->width, a == f->next);
	if (a != f->prev) comp_pos(f->parent, &row, &col);
	*alt = a;
	return frame2win(a);
}

static int one_window(Editor *e) { return !e->wins->next; }

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
			if (nf->layout == (vert ? FR_ROW : FR_COL))
				while (f->next && (vert ? frame2win(f)->x + f->width <= w->x + wcol
				                        : frame2win(f)->y + f->height <= w->y + wrow))
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
	fix_height(cw); fix_height(w);
	fix_width(cw); fix_width(w);
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
		fix_height(w1); fix_height(w2);
		fix_width(w1); fix_width(w2);
		win_comp_pos(e);
	}
	return 0;
}

/* win_splitmove(): CTRL-W H J K L; a count is the new size */
static int splitmove(Editor *e, int flags, int size)
{
	Win *w = e->win;
	int h = w->h, dir;
	Frame *alt;
	frame_take_out(e, w, &dir, &alt);
	win_unlink(e, w);
	win_comp_pos(e);
	/* win_split_ins() with the window itself; the alternate frame is
	   flattened right after, as Neovim does with to_flatten */
	e->win = frame2win(alt);
	flatten(e, alt);
	/* curwin is still w in Neovim, so win_enter() does nothing and
	   'winheight' / 'winwidth' do not apply (measured: 7 CTRL-W K gives
	   the equalized 6 lines, in 15 rows) */
	e->win = w;
	if (!split_ins(e, size, flags, w, dir)) return -1;
	if (!size && !(flags & WSP_VERT)) {
		win_setheight(e, w, h);
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
	int ch = cmdheight(e), nch = ch + t->height - height;
	if (nch < 1) nch = 1;
	if (nch != ch) {
		/* command_height(): the last full-width frame gives or takes the lines */
		Frame *f = e->wins->frame;
		Win *l = e->wins;
		while (l->next) l = l->next;
		f = l->frame;
		while (f->width != e->cols && f->parent) f = f->parent;
		int old = ch;
		while (nch > old && f) {
			int h = nch - old < f->height - minheight(f, NULL) ? nch - old : f->height - minheight(f, NULL);
			new_height(e, f, f->height - h, 0);
			for (Frame *p = f->parent; p; p = p->parent) p->height -= h;
			old += h;
			f = f->prev;
		}
		if (nch < old && f) {
			new_height(e, f, f->height + old - nch, 0);
			for (Frame *p = f->parent; p; p = p->parent) p->height += old - nch;
		}
		ch = nch < old ? nch : old;   /* E36 keeps what could be taken */
	}
	if (height > e->rows - ch) height = e->rows - ch;   /* ROWS_AVAIL */
	new_height(e, t, height, 0);
}

/* frame_setheight() */
static void setheight(Editor *e, Frame *cur, int height)
{
	if (cur->height == height) return;
	if (!cur->parent) { if (height > 0) top_new_height(e, height); return; }
	if (cur->parent->layout == FR_ROW) {
		int h = minheight(cur->parent, NULL);
		setheight(e, cur->parent, height > h ? height : h);
		return;
	}
	int room = 0, room_cmdline = 0;
	for (int run = 1; run <= 2; run++) {
		room = 0;
		for (Frame *f = cur->parent->child; f; f = f->next) {
			room += f->height;
			if (f != cur) room -= minheight(f, NULL);
		}
		if (cur->width != e->cols) room_cmdline = 0;
		else {
			Win *l = e->wins;
			while (l->next) l = l->next;
			room_cmdline = e->rows - cmdheight(e) - (l->y + l->h + STATUS);
			if (room_cmdline < 0) room_cmdline = 0;
		}
		if (height <= room + room_cmdline) break;
		if (run == 2 || cur->width == e->cols) { height = room + room_cmdline; break; }
		setheight(e, cur->parent, height + minheight(cur->parent, NULL) - WMH - 1);
	}
	int take = height - cur->height;
	if (take > 0 && room_cmdline > 0) {
		if (room_cmdline > take) room_cmdline = take;
		take -= room_cmdline;
		e->layout->height += room_cmdline;
	}
	new_height(e, cur, height, 0);
	for (int run = 0; run < 2; run++)
		for (Frame *f = run ? cur->prev : cur->next; f && take; f = run ? f->prev : f->next) {
			int h = minheight(f, NULL);
			if (f->height - take < h) { take -= f->height - h; new_height(e, f, h, 0); }
			else { new_height(e, f, f->height - take, 0); take = 0; }
		}
}

static void win_setheight(Editor *e, Win *w, int height)
{
	if (height < WMH) height = WMH;
	setheight(e, w->frame, height + STATUS);
	win_comp_pos(e);
}

/* frame_setwidth() */
static void setwidth(Editor *e, Frame *cur, int width)
{
	if (cur->width == width || !cur->parent) return;
	if (cur->parent->layout == FR_COL) {
		int w = minwidth(cur->parent, NULL);
		setwidth(e, cur->parent, width > w ? width : w);
		return;
	}
	int room = 0;
	for (int run = 1; run <= 2; run++) {
		room = 0;
		for (Frame *f = cur->parent->child; f; f = f->next) {
			room += f->width;
			if (f != cur) room -= minwidth(f, NULL);
		}
		if (width <= room) break;
		if (run == 2 || cur->height >= e->rows) { width = room; break; }
		setwidth(e, cur->parent, width + minwidth(cur->parent, NULL) - WMW - 1);
	}
	int take = width - cur->width;
	new_width(e, cur, width, 0);
	for (int run = 0; run < 2; run++)
		for (Frame *f = run ? cur->prev : cur->next; f && take; f = run ? f->prev : f->next) {
			int w = minwidth(f, NULL);
			if (f->width - take < w) { take -= f->width - w; new_width(e, f, w, 0); }
			else { new_width(e, f, f->width - take, 0); take = 0; }
		}
}

static void win_setwidth(Editor *e, Win *w, int width)
{
	if (w == e->win && width < WMW) width = WMW;
	if (width < 0) width = 0;
	setwidth(e, w->frame, width + w->frame->vsep);
	win_comp_pos(e);
}

void core_resize(Editor *e, int vertical, long n, int set)
{
	Win *w = e->win;
	if (n > 100000) n = 100000;
	if (n < -100000) n = -100000;
	if (vertical) win_setwidth(e, w, set ? (n ? (int)n : e->cols) : w->w + (int)n);
	else win_setheight(e, w, set ? (n ? (int)n : e->rows - 1) : w->h + (int)n);
}

void core_equal(Editor *e) { win_equal(e, NULL, 0, 'b'); }
