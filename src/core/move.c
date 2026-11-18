/* Scrolling, as Neovim's move.c, without folds, filler lines, concealing
   or 'smoothscroll' (none is on), except where CTRL-D, CTRL-U, CTRL-F and
   CTRL-B switch 'smoothscroll' on for the moment as Neovim does.  Lines are
   counted from 0 here and from 1 in Neovim: Neovim's "lnum == 1" is "l == 0",
   its w_botline (the line below the window) is bot, at most the line count. */
#include "core_int.h"
#include "uni.h"

typedef long lnr;
#define HUGE 1000000000

static long p_so = SCROLLOFF;
static long so_value(void) { return p_so; }
/* 'smoothscroll': off, but on while scroll_with_sms() runs */
static int p_sms;

static size_t digits(size_t n) { size_t d = 1; while (n >= 10) { n /= 10; d++; } return d; }

/* number_width() + 1 with number and relativenumber: numberwidth 4 */
int win_numw(const Win *w)
{
	size_t d = digits(buf_lines(w->buf));
	return (d < 3 ? 3 : (int)d) + 1;
}

static int width1(const Win *w) { return w->w - win_numw(w); }

static lnr nlines(const Win *w) { return (lnr)buf_lines(w->buf); }

/* the line's width, linetabsize() */
static long linesize(const Win *w, lnr l) { return (long)win_vcol_end(w, (size_t)l); }

/* plines_win_nofill(): screen lines of line l */
static int plines_lim(const Win *w, lnr l, int limit)
{
	if (!w->wrap || w->w == 0) return 1;
	long col = linesize(w, l);
	if (col == 0) return 1;
	long width = width1(w);
	if (width <= 0) return 32000 > w->h && limit ? w->h : 32000;
	if (col <= width) return 1;
	long n = (col - width + width - 1) / width + 1;
	if (limit && n > w->h) return w->h;
	return n > HUGE ? HUGE : (int)n;
}

int win_plines(const Win *w, size_t l) { return plines_lim(w, (lnr)l, 1); }

/* plines_win_col(): screen lines up to col of line l, a tab under the
   cursor in normal mode counted to its end.  Its callers pass a byte
   column but it counts that many characters, which reaches further on a
   line with multi-byte characters: copied (measured, review of PR 20, 5) */
static int plines_col(Editor *e, Win *w, lnr l, size_t col)
{
	if (!w->wrap || w->w == 0) return 1;
	LineIt it;
	const unsigned char *s = NULL;
	size_t v = 0, i = 0, k;
	lit_init(&it, w->buf, (size_t)l);
	while ((k = lit_next(&it, &s))) {
		if (i >= col) break;
		v += (size_t)core_cellw(w, s, k, 0, v);
		i++;
	}
	if (k && *s == '\t' && e->mode != MODE_INSERT) v += (size_t)core_cellw(w, s, k, 0, v) - 1;
	long width = width1(w);
	if (width <= 0) return 9999;
	int lines = 1;
	if ((long)v > width) lines += (int)(((long)v - width) / width) + 1;
	return lines;
}

/* screen lines of l, less those skipcol hides when it is the top line
   (plines_correct_topline()) */
static int plines_top(const Win *w, lnr l, int limit)
{
	int n = plines_lim(w, l, 0);
	if (l == (lnr)w->top && w->skipcol) {
		long wd = width1(w);
		if ((long)w->skipcol >= wd && wd > 0) n -= (int)(((long)w->skipcol - wd) / wd + 1);
	}
	if (limit && n > w->h) return w->h;
	return n;
}

/* comp_botline(): the line below the window; also the cursor line's row
   and height and the empty rows below the text */
typedef struct { lnr bot; int empty, crow, cheight; } Bot;

static Bot comp_botline(const Win *w)
{
	Bot b = { 0, 0, 0, 0 };
	lnr l = (lnr)w->top, n = nlines(w);
	int done = 0;
	for (; l < n; l++) {
		int k = plines_top(w, l, 1);
		/* curs_rows(): a changed line's height is plines_win_full(),
		   which skipcol does not shorten */
		if (l == (lnr)w->line) { b.crow = done; b.cheight = plines_lim(w, l, 1); }
		if (done + k > w->h) break;
		done += k;
	}
	b.bot = l;
	b.empty = done == 0 ? 0 : w->h - done;
	return b;
}

/* validate_botline_win() */
static void validate_botline(Win *w)
{
	WinState *s = &w->frame->s;
	if (s->bv & BOT_EXACT) return;
	Bot b = comp_botline(w);
	s->bot = (size_t)b.bot;
	s->empty = b.empty;
	s->bv = BOT_EXACT | BOT_AP;
}

size_t win_botline(Editor *e, Win *w, int *empty)
{
	(void)e;
	Bot b = comp_botline(w);
	if (empty) *empty = b.empty;
	return (size_t)b.bot;
}

void win_check_cursor(Win *w)
{
	size_t n = buf_lines(w->buf);
	if (w->line >= n) w->line = n - 1;
	if (w->top >= n) w->top = n - 1;
	/* check_cursor_col(): outside Insert mode (no window is entered in
	   it) on the first byte of a character (mb_adjust_cursor()), at most
	   the last; a reload or an undo can leave the column anywhere */
	LineIt it;
	const unsigned char *c;
	size_t p = 0, last = 0, k;
	lit_init(&it, w->buf, w->line);
	while ((k = lit_next(&it, &c)) && p + k <= w->col) { last = p; p += k; }
	w->col = k ? p : last;
}

/* the cursor's line without folds, in the top line's coordinates */
typedef struct { lnr l; int height; } Loff;

static void topline_back(const Win *w, Loff *lp, int limit)
{
	lp->l--;
	lp->height = lp->l < 0 ? HUGE : plines_lim(w, lp->l, limit);
}

static void botline_forw(const Win *w, Loff *lp)
{
	lp->l++;
	lp->height = lp->l >= nlines(w) ? HUGE : plines_lim(w, lp->l, 1);
}

static void set_topline(Win *w, lnr l)
{
	WinState *s = &w->frame->s;
	s->bot += (size_t)l - w->top;  /* approximate w_botline */
	if (s->bot > buf_lines(w->buf)) s->bot = buf_lines(w->buf);
	w->top = (size_t)l;
	s->topvalid = 0;      /* scrolloff is still to be checked */
	s->bv &= ~BOT_EXACT;
}

/* changed_line_abv_curs_win() with invalidate_botline_win() (changed_lines()) */
void win_invalidate(Win *w) { w->frame->s.topvalid = 0; w->frame->s.bv = 0; w->frame->s.sideok = 0; }

/* check_cursor_moved(): the view is looked at again once the cursor
   leaves its line or 'wrap' changes; w_botline once skipcol changes */
static void check_moved(Win *w)
{
	WinState *s = &w->frame->s;
	if (w->line != s->vline || w->wrap != s->wrap) {
		s->topvalid = 0;
		s->vline = w->line;
		s->vskip = w->skipcol;
		s->wrap = w->wrap;
	} else if (w->skipcol != s->vskip) {
		s->bv = 0;
		s->vskip = w->skipcol;
	}
}

static void set_valid(Win *w)
{
	check_moved(w);
	w->frame->s.topvalid = 1;
}

/* scroll_cursor_halfway() */
void scroll_halfway(Editor *e, Win *w, int atend, int prefer_above)
{
	(void)e;
	Loff lo = { (lnr)w->line, 0 }, bo = { (lnr)w->line, 0 };
	int used = plines_lim(w, (lnr)w->line, 1);
	lnr top = (lnr)w->line;
	while (top > 0) {
		int done = 0, above = 0, below = 0;
		for (int round = 1; round <= 2; round++) {
			if (prefer_above ? (round == 2 && below < above) : (round == 1 && below <= above)) {
				if (bo.l < nlines(w) - 1) {
					botline_forw(w, &bo);
					used += bo.height;
					if (used > w->h) { done = 1; break; }
					below += bo.height;
				} else {
					below++;
					if (atend) used++;
				}
			}
			if (prefer_above ? (round == 1 && below >= above) : (round == 1 && below > above)) {
				topline_back(w, &lo, 1);
				used = lo.height == HUGE ? HUGE : used + lo.height;
				if (used > w->h) { done = 1; break; }
				above += lo.height;
				top = lo.l;
			}
		}
		if (done) break;
	}
	w->top = (size_t)top;
	/* only with 'smoothscroll' (reset_skipcol()): without it a line taller
	   than the window keeps its view (second review of PR 20, B3) */
	if (p_sms && w->wrap) w->skipcol = 0;
	w->frame->s.topvalid = 1;
	w->frame->s.bv = 0;
}

/* scroll_cursor_top() */
static void scroll_cursor_top(Editor *e, Win *w, int min_scroll, int always)
{
	lnr old = (lnr)w->top;
	long off = so_value();
	int scrolled = 0, used = plines_lim(w, (lnr)w->line, 1);
	if ((lnr)w->line < old) scrolled = used;
	lnr top = (lnr)w->line - 1, bot = (lnr)w->line + 1, new_top = top + 1;
	int extra = 0;
	while (top >= 0) {
		int i = plines_lim(w, top, 1);
		if (top < (lnr)w->top) scrolled += i;
		if ((new_top >= (lnr)w->top || scrolled > min_scroll) && extra >= off) break;
		used += i;
		if (extra + i <= off && bot + 1 < nlines(w)) used += plines_lim(w, bot, 1);
		if (used > w->h) break;
		extra += i;
		new_top = top;
		top--;
		bot++;
	}
	if (used > w->h) scroll_halfway(e, w, 0, 0);
	else {
		size_t oskip = w->skipcol;
		if (new_top < (lnr)w->top || always) w->top = (size_t)new_top;
		if (w->top > w->line) w->top = w->line;
		if ((lnr)w->top != old) w->skipcol = 0;
		else if (w->top == w->line && w->skipcol >= win_vcol(e, w, NULL, NULL)) w->skipcol = 0;
		if ((lnr)w->top != old || w->skipcol != oskip) w->frame->s.bv = 0;
		w->frame->s.topvalid = 1;
	}
}

static int scrollup(Editor *e, Win *w, long count);

/* scroll_cursor_bot(): the cursor line near the bottom with scrolloff, or
   with set_topbot (zb) right at it */
static void scroll_cursor_bot(Editor *e, Win *w, int min_scroll, int set_topbot)
{
	lnr cln = (lnr)w->line, n = nlines(w);
	WinState *s = &w->frame->s, old = *s;
	size_t old_top = w->top, old_skip = w->skipcol;
	Bot b;
	if (set_topbot) {
		int used = 0;
		Loff lo = { cln + 1, 0 };
		for (;;) {
			topline_back(w, &lo, 0);
			if (lo.height == HUGE || used + lo.height > w->h) break;
			w->top = (size_t)lo.l;
			used += lo.height;
		}
		b.bot = cln + 1;
		b.empty = used == 0 ? 0 : w->h - used;
		s->bot = (size_t)b.bot;
		s->empty = b.empty;
		s->bv = BOT_EXACT | BOT_AP;
		if (w->top != old_top || w->skipcol) w->skipcol = 0;
	} else {
		validate_botline(w);
		b.bot = (lnr)s->bot;
		b.empty = s->empty;
	}
	int used = plines_lim(w, cln, 1), scrolled = 0, extra = 0;
	if (cln >= b.bot) {
		scrolled = used;
		if (cln == b.bot) scrolled -= b.empty;
	}
	Loff lo = { cln, 0 }, bo = { cln, 0 };
	long so = so_value();
	while (lo.l > 0) {
		if ((((scrolled <= 0 || scrolled >= min_scroll) && extra >= so) || bo.l + 1 >= n) && lo.l <= b.bot)
			break;
		lnr before = lo.l;
		topline_back(w, &lo, 1);
		used = lo.height == HUGE ? HUGE : used + lo.height;
		if (used > w->h) break;
		if (lo.l >= b.bot) {
			scrolled += lo.height;
			if (lo.l == b.bot && before > b.bot) scrolled -= b.empty;
		}
		if (bo.l + 1 < n) {
			lnr bbefore = bo.l;
			botline_forw(w, &bo);
			used += bo.height;
			if (used > w->h) break;
			if (extra < so || scrolled < min_scroll) {
				extra += bo.height;
				if (bo.l >= b.bot) {
					scrolled += bo.height;
					if (bbefore < b.bot) scrolled -= b.empty;
				}
			}
		}
	}
	long line_count = 0;
	if (scrolled <= 0) line_count = 0;
	else if (used > w->h) line_count = used;
	else {
		Loff f = { (lnr)w->top - 1, 0 };
		int i;
		for (i = 0; i < scrolled && f.l < b.bot;) {
			botline_forw(w, &f);
			i += f.height;
			line_count++;
		}
		if (i < scrolled) line_count = 9999;   /* below the window: don't scroll */
	}
	if (line_count >= w->h && line_count > min_scroll) scroll_halfway(e, w, 0, 1);
	else if (line_count > 0) scrollup(e, w, line_count);
	/* topline unchanged: w_botline and w_empty_rows as they were */
	if (set_topbot && w->top == old_top && w->skipcol == old_skip) {
		s->bot = old.bot;
		s->empty = old.empty;
		s->bv = old.bv;
	}
	s->topvalid = 1;
}

/* check_top_offset(): fewer than scrolloff screen lines above the cursor */
static int check_top_offset(const Win *w)
{
	long so = so_value();
	if ((lnr)w->line < (lnr)w->top + so) {
		Loff lo = { (lnr)w->line, 0 };
		int n = 0;
		while (n < so) {
			topline_back(w, &lo, 1);
			if (lo.l < (lnr)w->top) break;
			n += lo.height;
		}
		if (n < so) return 1;
	}
	return 0;
}

/* update_topline() */
static void update_topline(Editor *e, Win *w)
{
	long so = so_value();
	lnr n = nlines(w);
	int check_botline = 0;
	if (w->h == 0) { w->top = w->line; return; }
	check_moved(w);
	if (w->frame->s.topvalid) return;
	w->frame->s.topvalid = 1;
	if (n == 1 && !linesize(w, 0)) {          /* buf_is_empty() */
		w->top = 0;
		w->skipcol = 0;
		w->frame->s.bot = 1;
		w->frame->s.bv = BOT_EXACT | BOT_AP;
		return;
	}
	lnr old_top = (lnr)w->top;
	int check_topline = 0;
	if (w->top > 0 || w->skipcol > 0) {
		if (w->line < w->top) check_topline = 1;
		else if (check_top_offset(w)) check_topline = 1;
		else if (w->skipcol > 0 && w->line == w->top) {
			/* the cursor's first cell against skipcol and the "<<<" marker
			   (sms_marker_overlap(): 3 cells less the number column) */
			size_t v;
			win_vcol(e, w, &v, NULL);
			int nw = win_numw(w);
			if ((long)w->skipcol + (nw > 3 ? 0 : 3 - nw) > (long)v) check_topline = 1;
		}
	}
	if (check_topline) {
		int half = w->h / 2 - 1;
		if (half < 2) half = 2;
		long d = (lnr)w->top + so - (lnr)w->line;
		if (d >= half) scroll_halfway(e, w, 0, 0);
		else { scroll_cursor_top(e, w, 0, 0); check_botline = 1; }
	} else check_botline = 1;
	if (check_botline) {
		/* an approximate w_botline is used as it is */
		if (!(w->frame->s.bv & BOT_AP)) validate_botline(w);
		Bot b = { (lnr)w->frame->s.bot, w->frame->s.empty, 0, 0 };
		if (b.bot < n) {
			if ((lnr)w->line < b.bot) {
				if ((lnr)w->line >= b.bot - so) {
					int k = b.empty;
					Loff lo = { (lnr)w->line, 0 };
					while (lo.l < b.bot) {
						k += lo.height;
						if (k >= so) break;
						botline_forw(w, &lo);
					}
					if (k >= so) check_botline = 0;
				} else check_botline = 0;
			}
			if (check_botline) {
				long d = (lnr)w->line - b.bot + 1 + so;
				if (d <= w->h + 1) scroll_cursor_bot(e, w, 1, 0);   /* 'scrolljump' 1 */
				else scroll_halfway(e, w, 0, 0);
			}
		}
	}
	if ((lnr)w->top != old_top) w->skipcol = 0;
}

/* curs_columns(): the cursor's row and column in the window; scrolls
   sideways without wrap, sets skipcol for a line taller than the window */
static void curs_columns(Editor *e, Win *w, int may_scroll)
{
	update_topline(e, w);
	Bot b = comp_botline(w);
	size_t start, end;
	size_t vcol = win_vcol(e, w, &start, &end);
	int extra = win_numw(w);
	long wcol = (long)vcol + extra, endcol = (long)end + extra;
	long wrow = b.crow;
	long w1 = w->w - extra, w2 = 0;
	int did_sub = 0;
	if (w1 <= 0) {
		wcol = w->w - 1;
		wrow = w->h - 1 - (w->wrap ? 0 : b.empty);
	} else if (w->wrap && w->w) {
		w2 = w1;
		if (w->line == w->top && w->skipcol > 0 && wcol >= (long)w->skipcol) {
			if ((long)w->skipcol <= w1) wcol -= w2;
			else wcol -= w2 * (((long)w->skipcol - w1) / w2 + 1);
			did_sub = 1;
		}
		if (wcol >= w->w) {
			long k = (wcol - w->w) / w2 + 1;
			wcol -= k * w2;
			wrow += k;
		}
	} else if (may_scroll) {
		long off_left = (long)start - (long)w->left;
		long off_right = endcol - (long)w->left - w->w + 1;
		/* validate_cursor() scrolls only for a cursor or view that moved:
		   a tab across the left edge stays as it was scrolled to (third
		   review of PR 20, c_narrow_0) */
		WinState *s = &w->frame->s;
		int valid = s->sideok && s->sline == w->line && s->scol == w->col && s->sleft == w->left
		            && s->sw == w->w;
		if (!valid && (off_left < 0 || off_right > 0)) {
			long diff = off_left < 0 ? -off_left : off_right;
			long nl;
			/* sidescroll 1, sidescrolloff 0 */
			if (diff >= w1 / 2 || off_right >= off_left) nl = wcol - extra - w1 / 2;
			else nl = off_left < 0 ? (long)w->left - diff : (long)w->left + diff;
			if (nl < 0) nl = 0;
			w->left = (size_t)nl;
		}
		s->sideok = 1; s->sline = w->line; s->scol = w->col; s->sleft = w->left; s->sw = w->w;
		wcol -= (long)w->left;
	} else if (wcol > (long)w->left) wcol -= (long)w->left;
	else wcol = 0;
	long so = so_value();
	size_t prev_skip = w->skipcol;
	long plines = 0;
	if ((wrow >= w->h || ((prev_skip > 0 || wrow + so >= w->h)
	                      && (plines = plines_lim(w, (lnr)w->line, 0)) - 1 >= w->h))
	    && w->h && w->line == w->top && w2 > 0 && w->w) {
		long ex = 0, n;
		if ((long)w->skipcol + so * w2 > (long)vcol) ex = 1;
		if (!plines) plines = plines_lim(w, (lnr)w->line, 0);
		plines--;
		n = plines > wrow + so ? wrow + so : plines;
		if (n >= w->h + (long)w->skipcol / w2 - so) ex += 2;
		if (ex == 3 || w->h <= so * 2) {
			n = (long)vcol / w2;
			n = n > w->h / 2 ? n - w->h / 2 : 0;
			if (n > plines - w->h + 1) n = plines - w->h + 1;
			w->skipcol = n > 0 ? (size_t)(w1 + (n - 1) * w2) : 0;
		} else if (ex == 1) {
			ex = ((long)w->skipcol + so * w2 - (long)vcol + w2 - 1) / w2;
			if (ex > 0) {
				if (ex * w2 > (long)w->skipcol) ex = (long)w->skipcol / w2;
				w->skipcol -= (size_t)(ex * w2);
			}
		} else if (ex == 2) {
			long ec = (n - w->h + 1) * w2;
			while (ec > (long)vcol) ec -= w2;
			if (ec > (long)w->skipcol) w->skipcol = (size_t)ec;
		}
		if (did_sub) wrow -= ((long)w->skipcol - (long)prev_skip) / w2;
		else wrow -= (long)w->skipcol / w2;
		if (wrow >= w->h) {
			ex = wrow - w->h + 1;
			w->skipcol += (size_t)(ex * w2);
			wrow -= ex;
		}
	} else if (!p_sms) w->skipcol = 0;
	w->frame->s.wrow = (int)wrow;
	w->frame->s.wcol = (int)wcol;
	w->frame->s.crow = b.crow;
	w->frame->s.cheight = b.cheight;
}

void win_update(Editor *e, Win *w) { curs_columns(e, w, 1); }

/* the end of do_ecmd(): curwin_init()'s topline 1, then update_topline()
   with 'scrolloff' 999, which centres the cursor line */
void win_ecmd_topline(Editor *e, Win *w)
{
	w->top = 0;
	w->skipcol = 0;
	w->frame->s.topvalid = 0;
	w->frame->s.bot = 1;           /* curwin_init(): w_botline 2, w_valid 0 */
	w->frame->s.bv = 0;
	p_so = 999;
	update_topline(e, w);
	p_so = SCROLLOFF;
}
void core_update(Editor *e) { curs_columns(e, e->win, 1); }

/* win_update() for each window: skipcol made valid, then w_botline as drawn.  update_topline() may have
   used an approximate one, so for the current window a drawn value that
   differs looks at the view again, once (drawscreen.c, "There is a trick
   with w_botline") */
void win_drawn(Editor *e)
{
	for (Win *w = e->wins; w; w = w->next) {
		/* win_update()'s start: skipcol, set for another width, rounded
		   down to a row start */
		long w1 = width1(w);
		if (w->skipcol > 0 && w1 > 0)
			w->skipcol = (long)w->skipcol < w1 ? 0 : (size_t)(w1 + ((long)w->skipcol - w1) / w1 * w1);
		WinState *s = &w->frame->s;
		size_t old = s->bot, top = w->top, skip = w->skipcol;
		for (int again = w == e->win; ; again = 0) {
			Bot b = comp_botline(w);
			s->bot = (size_t)b.bot;
			s->empty = b.empty;
			s->bv |= BOT_EXACT;
			if (!again || s->bot == old) break;
			s->topvalid = 0;
			update_topline(e, w);
			if (w->top == top && w->skipcol == skip) break;   /* no redraw */
			curs_columns(e, w, 1);
		}
	}
}

void win_set_fraction(Win *w)
{
	/* FRACTION_MULT 16384 */
	if (w->h > 1) w->frame->s.fraction = (w->frame->s.wrow * 16384 + 16384 / 2) / w->h;
}

/* scroll_to_fraction() */
static void scroll_to_fraction(Editor *e, Win *w, int prev_height)
{
	int height = w->h;
	if (height > 0 && (height < nlines(w) || w->top > 0)) {
		lnr l = (lnr)w->line;
		int wrow = (w->frame->s.fraction * height - 1) / 16384;
		int line_size = plines_col(e, w, l, w->col) - 1;
		int sline = wrow - line_size;
		if (sline >= 0) {
			int rows = plines_lim(w, l, 0);
			if (sline > height - rows) {
				sline = height - rows;
				wrow -= rows - line_size;
			}
		}
		if (sline < 0) {
			wrow = line_size;
			if (wrow >= height && w->w - win_numw(w) > 0) {
				w->skipcol += (size_t)(w->w - win_numw(w));
				wrow--;
				while (wrow >= height) {
					w->skipcol += (size_t)(w->w - win_numw(w));
					wrow--;
				}
			}
		} else if (sline > 0) {
			while (sline > 0 && l > 0) {
				l--;
				line_size = plines_lim(w, l, 1);
				sline -= line_size;
			}
			if (sline < 0) {
				l++;
				wrow -= line_size + sline;
			} else if (sline > 0) {
				l = 0;
				wrow -= sline;
			}
		}
		w->frame->s.wrow = wrow;
		set_topline(w, l);
	}
	if (w == e->win) curs_columns(e, w, 0);
	if (prev_height > 0) w->frame->s.prev_fraction_row = w->frame->s.wrow;
	w->frame->s.bv = 0;
}

/* win_new_height() and win_set_inner_size(): keep the cursor at the same
   fraction of the window (splitkeep=cursor) */
void win_new_height(Editor *e, Win *w, int h)
{
	if (h < 0) h = 0;
	if (w->h == h) return;
	int prev = w->h;
	if (h > 0) {
		if (w == e->win) curs_columns(e, w, 1);
		if (w->frame->s.wrow != w->frame->s.prev_fraction_row) win_set_fraction(w);
	}
	w->h = h;
	w->scroll = 0;
	w->skipcol = 0;
	scroll_to_fraction(e, w, prev);
}

void win_new_width(Editor *e, Win *w, int wd)
{
	if (wd < 0) wd = 0;
	if (w->w == wd) return;
	w->w = wd;
	win_invalidate(w);
	if (w == e->win) curs_columns(e, w, 1);
}

void win_cursor_pos(Editor *e, Win *w, int *wrow, int *wcol)
{
	(void)e;
	*wrow = w->frame->s.wrow;
	*wcol = w->frame->s.wcol;
}

/* ---- scrolling commands ---- */

static void coladvance(Editor *e, Win *w)
{
	w->col = core_col_at(e, w->line, w->want, e->mode == MODE_INSERT);
}

/* cursor_correct(): scrolloff lines above and below the cursor if possible */
static void cursor_correct(Editor *e, Win *w)
{
	long above_wanted = so_value(), below_wanted = so_value();
	if (w->top == 0) {
		above_wanted = 0;
		if (below_wanted > w->h / 2) below_wanted = w->h / 2;
	}
	validate_botline(w);
	Bot b = comp_botline(w);
	if (b.bot == nlines(w)) {
		below_wanted = 0;
		if (above_wanted > (w->h - 1) / 2) above_wanted = (w->h - 1) / 2;
	}
	lnr cln = (lnr)w->line;
	if (cln >= (lnr)w->top + above_wanted && cln < b.bot - below_wanted) return;
	/* 'smoothscroll' without wrap: a cursor line as high as the window stays */
	if (p_sms && !w->wrap && w->frame->s.cheight == w->h) { w->skipcol = 0; return; }
	lnr top = (lnr)w->top, bot = b.bot - 1;
	int above = 0, below = 0;
	while ((above < above_wanted || below < below_wanted) && top < bot) {
		if (below < below_wanted && (below <= above || above >= above_wanted)) {
			below += plines_lim(w, bot, 1);
			bot--;
		}
		if (above < above_wanted && (above < below || below >= below_wanted)) {
			above += plines_lim(w, top, 1);
			top++;
		}
	}
	if (top == bot || bot < 0) w->line = (size_t)top;
	else if (top > bot) w->line = (size_t)bot;
	else {
		if (cln < top && w->top > 0) w->line = (size_t)top;
		if (cln > bot && b.bot < nlines(w)) w->line = (size_t)bot;
	}
	set_valid(w);
	(void)e;
}

/* scrolldown() (CTRL-Y) */
static int scrolldown(Editor *e, Win *w, long count)
{
	long w1 = width1(w), w2 = w1;
	int done = 0;
	int sms = p_sms && w->wrap;
	curs_columns(e, w, 1);         /* validate_cursor(): w_wrow */
	for (long todo = count; todo > 0; todo--) {
		if (w->top == 0 && (!sms || (long)w->skipcol < w1)) break;
		if (sms && (long)w->skipcol >= w1) {
			if ((long)w->skipcol >= w1 + w2) w->skipcol -= (size_t)w2;
			else w->skipcol -= (size_t)w1;
			done++;
		} else {
			w->top--;
			w->skipcol = 0;
			if (sms) {
				long size = linesize(w, (lnr)w->top);
				if (size > w1) { w->skipcol = (size_t)w1; size -= w1; }
				while (size > w2) { w->skipcol += (size_t)w2; size -= w2; }
				done++;
			} else done += plines_lim(w, (lnr)w->top, 1);
			w->frame->s.bot--;     /* approximate w_botline, then invalidate_botline_win() */
			w->frame->s.bv = 0;
		}
	}
	/* the cursor's last screen row, from w_wrow kept up to date; moved up
	   into the window */
	int wrow = w->frame->s.wrow + done;
	if (w->wrap && w->w) {
		check_moved(w);        /* validate_cheight(): a skipcol change drops w_botline */
		wrow += plines_lim(w, (lnr)w->line, 1) - 1 - (int)(win_vcol(e, w, NULL, NULL) / (size_t)w->w);
	}
	int moved = 0;
	while (wrow >= w->h && w->line > 0) {
		wrow -= plines_lim(w, (lnr)w->line, 1);
		w->line--;
		moved = 1;
	}
	if (moved) coladvance(e, w);
	if (w->line < w->top) w->line = w->top;
	return moved;
}

/* scrollup() (CTRL-E) */
static int scrollup(Editor *e, Win *w, long count)
{
	lnr n = nlines(w);
	size_t top0 = w->top;
	int sms = p_sms && w->wrap;
	if (sms) {
		long w1 = width1(w), w2 = w1;
		long size = linesize(w, (lnr)w->top);
		for (long todo = count; todo > 0; todo--) {
			lnr l = (lnr)w->top;
			long add = w->skipcol > 0 ? w2 : w1;
			w->skipcol += (size_t)add;
			if ((long)w->skipcol >= size) {
				if (l == n - 1) { w->skipcol -= (size_t)add; break; }
				l++;
			}
			if (l > (lnr)w->top) {
				w->top = (size_t)l;
				w->skipcol = 0;
				if (todo > 1) size = linesize(w, l);
			}
		}
	} else w->top += (size_t)count;
	if ((lnr)w->top > n - 1) w->top = (size_t)(n - 1);
	WinState *s = &w->frame->s;
	s->bot += w->top - top0;       /* approximate w_botline */
	if (s->bot > (size_t)n) s->bot = (size_t)n;
	s->bv &= ~BOT_EXACT;
	if (w->line < w->top) {
		w->line = w->top;
		coladvance(e, w);
	}
	return w->top != top0;
}

/* cursor_correct_sms(): with 'smoothscroll' (on only inside
   scroll_with_sms() here) the cursor goes to the part of the top line in
   view.  No 'list' or 'showbreak', so sms_marker_overlap() is
   3 - win_col_off() at most. */
static void cursor_correct_sms(Editor *e, Win *w)
{
	if (!w->wrap || w->line != w->top) return;
	long so = so_value(), w1 = width1(w), w2 = w1;
	long so_cols = so == 0 ? 0 : w1 + (so - 1) * w2;
	long space = (long)(w->h - 1) * w2;
	long size = so == 0 ? 0 : linesize(w, (lnr)w->top);
	if (w->top == 0 && w->skipcol == 0) so_cols = 0;     /* top of buffer: no scrolloff */
	else if (so_cols > space / 2) so_cols = space / 2;
	while (so_cols > size && so_cols - w2 >= w1 && w1 > 0) so_cols -= w2;
	if (so_cols >= w1 && so_cols > size) so_cols -= w1;
	long ex = w->w - w2, overlap = w->skipcol == 0 ? 0 : ex > 3 ? 0 : 3 - ex;
	long top = (long)w->skipcol + (so_cols ? so_cols : overlap);
	long bot = (long)w->skipcol + w1 + (long)(w->h - 1) * w2 - so_cols;
	long v = (long)win_vcol(e, w, NULL, NULL), col = v;
	if (col < top) {
		if (col < w1) col += w1;
		while (w2 > 0 && col < top) col += w2;
	} else
		while (w2 > 0 && col >= bot) col -= w2;
	if (col == v) return;
	w->want = (size_t)col;
	coladvance(e, w);
	/* coladvance() fails past the line's end: if the cursor is still
	   above the part in view, it goes to the next line */
	if (col > 0 && col >= linesize(w, (lnr)w->line) && w->skipcol > 0 && w->line + 1 < buf_lines(w->buf)
	    && (long)win_vcol(e, w, NULL, NULL) < (long)w->skipcol + overlap) {
		w->line++;
		w->col = 0;
		w->want = 0;
	}
}

/* scroll_redraw(): scroll, keep scrolloff, and move the cursor at least a
   line if the view could not move; the view then stays (VALID_TOPLINE) */
static void scroll_redraw(Editor *e, Win *w, int up, long count)
{
	size_t ptop = w->top, pskip = w->skipcol, pline = w->line;
	if (up) scrollup(e, w, count);
	else scrolldown(e, w, count);
	cursor_correct(e, w);
	set_valid(w);
	while (w->top == ptop && w->skipcol == pskip) {
		if (up) {
			if (w->line > pline || w->line + 1 >= buf_lines(w->buf)) break;
			w->line++;
		} else {
			if (w->line < pline || ptop == 0 || w->line == 0) break;
			w->line--;
		}
		coladvance(e, w);      /* cursor_down(), cursor_up() */
		set_valid(w);
	}
	if (p_sms) cursor_correct_sms(e, w);
	if (w->line != pline) coladvance(e, w);
}

void core_scroll(Editor *e, long n)
{
	if (n > 0) scroll_redraw(e, e->win, 1, n);
	else if (n < 0) scroll_redraw(e, e->win, 0, -n);
	core_update(e);
}

/* scroll_with_sms(): scroll with 'smoothscroll' on, then finish a line left
   partly shown */
static int scroll_sms(Editor *e, Win *w, int dir, long count, long *curscount)
{
	size_t pskip = w->skipcol, ptop = w->top;
	p_sms = 1;
	scroll_redraw(e, w, dir > 0, count);
	/* a window narrower than its number column has no text width, where
	   Neovim's arithmetic overflows (measured: skipcol -2147483630 after
	   CTRL-D in a 3-column window with 'number'): nothing to finish */
	if (w->skipcol > 0 && width1(w) > 0) {
		int fix = dir;
		lnr d = (lnr)w->top - (lnr)ptop;
		if ((d < 0 ? -d : d) > (dir < 0)) fix = -dir;
		long w1 = width1(w), w2 = w1;
		long c = 1 + ((long)w->skipcol - w1 - 1) / w2;
		if (fix > 0) c = 1 + (linesize(w, (lnr)w->top) - (long)w->skipcol - w1 + w2 - 1) / w2;
		scroll_redraw(e, w, fix > 0, c);
		*curscount += c * (fix == dir ? 1 : -1);
	}
	p_sms = 0;
	return w->top != ptop || w->skipcol != pskip;
}

static int screengo(Editor *e, Win *w, int dir, long dist);

/* cursor_down_inner() and cursor_up_inner() */
static void cursor_move_lines(Win *w, long n)
{
	lnr l = (lnr)w->line + n, last = nlines(w) - 1;
	if (l < 0) l = 0;
	if (l > last) l = last;
	w->line = (size_t)l;
}

/* pagescroll() */
/* -1 when neither the view nor the cursor moved: Neovim beeps (measured:
   a mapping stops after CTRL-D or CTRL-F at the end, CTRL-U or CTRL-B at
   the top) */
static int pagescroll(Editor *e, int dir, long count, int half)
{
	Win *w = e->win;
	lnr n = nlines(w);
	size_t pcol = w->col, pwant = w->want, pline = w->line;
	int moved = 0;
	if (half) {
		if (count) w->scroll = (size_t)(count < w->h ? count : w->h);
		long sc = w->scroll ? (long)w->scroll : (w->h / 2 > 1 ? w->h / 2 : 1);
		count = sc < w->h ? sc : w->h;
		long curscount = count;
		if (dir > 0 && (lnr)w->top + w->h + count > n) {
			long k = plines_top(w, (lnr)w->top, 0);
			if (k - count < w->h && (lnr)w->top < n - 1) {
				long max = w->h + count;
				for (lnr l = (lnr)w->top + 1; l < n && k - plines_top(w, (lnr)w->top, 0) < max; l++)
					k += plines_lim(w, l, 0);
				if (k - plines_top(w, (lnr)w->top, 0) > max) k = plines_top(w, (lnr)w->top, 0) + max;
			}
			if (k < w->h + count) count = k - w->h;
		}
		if (count > 0) {
			moved = scroll_sms(e, w, dir, count, &curscount);
			w->line = pline;
			w->col = pcol;
			w->want = pwant;
		}
		if (w->wrap) screengo(e, w, dir, curscount);
		else cursor_move_lines(w, dir > 0 ? curscount : -curscount);
	} else {
		/* get_scroll_overlap() */
		validate_botline(w);
		Bot b = comp_botline(w);
		int minh = w->h - 2, ov;
		if ((dir < 0 && w->top == 0) || (dir > 0 && b.bot >= n)) ov = minh + 2;
		else {
			Loff lo = { dir > 0 ? b.bot : (lnr)w->top - 1, 0 };
			lo.height = plines_lim(w, lo.l, 1);
			int h1 = lo.height, h2, h3, h4;
			if (h1 > minh) ov = minh + 2;
			else {
				if (dir > 0) topline_back(w, &lo, 1); else botline_forw(w, &lo);
				h2 = lo.height;
				if (h2 == HUGE || h2 + h1 > minh) ov = minh + 2;
				else {
					if (dir > 0) topline_back(w, &lo, 1); else botline_forw(w, &lo);
					h3 = lo.height;
					if (h3 == HUGE || h3 + h2 > minh) ov = minh + 2;
					else {
						if (dir > 0) topline_back(w, &lo, 1); else botline_forw(w, &lo);
						h4 = lo.height;
						ov = h4 == HUGE || h4 + h3 + h2 > minh || h3 + h2 + h1 > minh ? minh + 1 : minh;
					}
				}
			}
		}
		count *= ov;
		moved = scroll_sms(e, w, dir, count, &count);
		if (moved) {
			validate_botline(w);
			b = comp_botline(w);
			lnr l = dir > 0 ? (lnr)w->top : b.bot - 1;
			w->line = (size_t)(l < 0 ? 0 : l);
		}
	}
	cursor_correct(e, w);
	moved = moved || pcol != w->col || pline != w->line;
	/* beginline(BL_SOL | BL_FIX) with nostartofline: back to curswant */
	if (moved) coladvance(e, w);
	core_update(e);
	return moved ? 0 : -1;
}

int core_scroll_half(Editor *e, int dir, long count) { return pagescroll(e, dir, count, 1); }
int core_scroll_page(Editor *e, int dir, long count) { return pagescroll(e, dir, count ? count : 1, 0); }

/* nv_zet(): zt zz zb, then set_fraction() */
void core_scroll_cursor(Editor *e, int how)
{
	Win *w = e->win;
	if (how == 't') scroll_cursor_top(e, w, 0, 1);
	else if (how == 'z') scroll_halfway(e, w, 1, 0);
	else scroll_cursor_bot(e, w, 0, 1);
	core_update(e);
	win_set_fraction(w);
}

/* nv_scroll(): H M L, then cursor_correct() for scrolloff, which Neovim
   leaves out under an operator (so 0) */
size_t core_screen_line(Editor *e, int how, long count, int so)
{
	Win *w = e->win;
	core_update(e);
	validate_botline(w);
	Bot b = comp_botline(w);
	lnr n = nlines(w), l;
	if (count < 1) count = 1;
	if (how == 'L') l = count - 1 >= b.bot ? 0 : b.bot - 1 - (count - 1);
	else {
		long k = 0;
		if (how == 'M') {
			int used = 0, half = (w->h - b.empty + 1) / 2;
			for (; (lnr)w->top + k < n - 1; k++) {
				used += plines_lim(w, (lnr)w->top + k, 1);
				if (used >= half) break;
			}
			if (k > 0 && used > w->h) k--;
		} else k = count - 1;
		l = (lnr)w->top + k < n - 1 ? (lnr)w->top + k : n - 1;
	}
	size_t keep = w->line;
	w->line = (size_t)l;
	if (so) cursor_correct(e, w);
	l = (lnr)w->line;
	w->line = keep;
	return (size_t)l;
}

/* nv_screengo() */
static int screengo(Editor *e, Win *w, int dir, long dist)
{
	long w1 = width1(w), w2 = w->w - win_numw(w), linelen = linesize(w, (lnr)w->line);
	int ok = 1, atend = 0;
	if (w2 == 0) w2 = 1;
	if (w->w) {
		long want = (long)w->want;
		if (w->want == WANT_EOL) {
			atend = 1;
			size_t v = win_vcol(e, w, NULL, NULL);
			if (w1 <= 0) want = 0;
			else {
				want = w1 - 1;
				if ((long)v > want) want += (((long)v - want - 1) / w2 + 1) * w2;
			}
		} else {
			long n = linelen > w1 ? ((linelen - w1 - 1) / w2 + 1) * w2 + w1 : w1;
			if (want > n - 1) want = n - 1;
		}
		while (dist--) {
			if (dir < 0) {
				if (want >= w1) want -= w2;
				else {
					if (w->line == 0) { ok = 0; break; }
					w->line--;
					linelen = linesize(w, (lnr)w->line);
					if (linelen > w1) want += (((linelen - w1 - 1) / w2) + 1) * w2;
				}
			} else {
				long n = linelen > w1 ? ((linelen - w1 - 1) / w2 + 1) * w2 + w1 : w1;
				if (want + w2 < n) want += w2;
				else {
					if ((lnr)w->line >= nlines(w) - 1) { ok = 0; break; }
					w->line++;
					want %= w2;
					if (want >= w1) want -= w2;
					linelen = linesize(w, (lnr)w->line);
				}
			}
		}
		w->want = (size_t)want;
	}
	coladvance(e, w);
	if (w->col > 0 && w->wrap) {
		size_t v = win_vcol(e, w, NULL, NULL);
		long want = (long)w->want;
		if ((long)v > want && (want < w1 ? want > w1 / 2 : (want - w1) % w2 > w2 / 2)) {
			size_t len;
			const unsigned char *s = core_line(e, w->line, &len);
			w->col = uni_gprev(s, len, w->col);
		}
	}
	if (atend) w->want = WANT_EOL;
	return ok;
}

int core_screen_move(Editor *e, long n)
{
	Win *w = e->win;
	int r;
	if (!w->wrap) {
		lnr l = (lnr)w->line + n;
		if (l < 0 || l >= nlines(w)) {
			if ((n < 0 && w->line == 0) || (n > 0 && (lnr)w->line == nlines(w) - 1)) return -1;
		}
		cursor_move_lines(w, n);
		coladvance(e, w);
		r = 0;
	} else r = screengo(e, w, n < 0 ? -1 : 1, n < 0 ? -n : n) ? 0 : -1;
	core_update(e);
	return r;
}
