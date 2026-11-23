/* Folds: Neovim 0.12's fold.c, the parts foldmethod=expr uses, with its
   names in the comments.  Inside, lines count from 1 and folds nest with
   line numbers relative to the fold holding them, as there: the port stays
   line for line comparable.  The foldexpr here gives only "n" and ">n", so
   foldlevelExpr()'s lvl_next, end and had_end never differ from lvl and
   "no end", and those branches are gone. */
#include <stdlib.h>
#include <string.h>
#include "fold.h"

enum { FD_OPEN, FD_CLOSED, FD_LEVEL };
enum { MAX_LEVEL = 20 };
#define MAXL 0x7fffffffL   /* MAXLNUM */

typedef struct Arr { struct Nd *d; int n, cap; } Arr;
typedef struct Nd {
	long top, len;     /* fd_top, fd_len */
	Arr nested;
	char flags;
} Nd;

struct Fold {
	Arr folds;         /* w_folds */
	int level, invalid;  /* w_p_fdl, w_foldinvalid */
	long n;            /* lines in the buffer, for a fold left past the end */
	FoldExpr *expr;
	FoldTall *tall;
	void *ctx;
};

/* fline_T.  Its lvl_next only matters for "a1", "s1", "<1" and "=" */
typedef struct { long lnum, off, lnum_save, n; int lvl, start; } Line;

static Nd *end(const Arr *a) { return a->d + a->n; }

/* ga_grow(): by at least half the length.  The new slots are zeroed so
   that reading the one past the end (recurse()) is defined.  Neovim grows
   by at least 10: no difference in 34,200 random scenarios (measured) */
static void grow(Arr *a, int k)
{
	if (a->cap - a->n >= k) return;
	if (k < a->n / 2) k = a->n / 2;
	Nd *d = realloc(a->d, (size_t)(a->n + k) * sizeof *d);
	if (!d) abort();   /* as Neovim's xrealloc(): the crash journal keeps the text */
	memset(d + a->cap, 0, (size_t)(a->n + k - a->cap) * sizeof *d);
	a->d = d;
	a->cap = a->n + k;
}

static void clear(Arr *a)   /* deleteFoldRecurse() */
{
	for (int i = 0; i < a->n; i++) clear(&a->d[i].nested);
	free(a->d);
	*a = (Arr){0};
}

/* foldFind(): 1 and *fp the fold holding lnum, or 0 and *fp the first
   fold below it (possibly end(a)), NULL when a is empty */
static int find(const Arr *a, long lnum, Nd **fp)
{
	if (!a->n) { *fp = NULL; return 0; }
	int lo = 0, hi = a->n - 1;
	while (lo <= hi) {
		int i = (lo + hi) / 2;
		if (a->d[i].top > lnum) hi = i - 1;
		else if (a->d[i].top + a->d[i].len <= lnum) lo = i + 1;
		else { *fp = a->d + i; return 1; }
	}
	*fp = a->d + lo;
	return 0;
}

static void del(Arr *a, int i)   /* deleteFoldEntry(), recursive */
{
	clear(&a->d[i].nested);
	a->n--;
	memmove(a->d + i, a->d + i + 1, (size_t)(a->n - i) * sizeof *a->d);
}

static void ins(Arr *a, int i)   /* foldInsert() */
{
	grow(a, 1);
	memmove(a->d + i + 1, a->d + i, (size_t)(a->n - i) * sizeof *a->d);
	a->n++;
	a->d[i].nested = (Arr){0};
}

/* foldMarkAdjustRecurse() */
static void adjust(Arr *a, long line1, long line2, long amount, long after, int insert)
{
	if (!a->n) return;
	/* in Insert mode a line inserted at the top of a fold joins it */
	long top = insert && amount == 1 && line2 == MAXL ? line1 + 1 : line1;
	Nd *fp;
	find(a, line1, &fp);
	for (int i = (int)(fp - a->d); i < a->n; i++, fp++) {
		long last = fp->top + fp->len - 1;
		if (last < line1) continue;             /* 1: above line1 */
		if (fp->top > line2) {                  /* 6: below line2 */
			if (!after) break;
			fp->top += after;
		} else if (fp->top >= top && last <= line2) {   /* 4: inside */
			if (amount == MAXL) { del(a, i); i--; fp--; }
			else fp->top += amount;
		} else if (fp->top < top) {             /* 2, 3: holds line1 */
			adjust(&fp->nested, line1 - fp->top, line2 - fp->top, amount, after, insert);
			if (last <= line2) {                /* 2: line2 below it */
				if (amount == MAXL) fp->len = line1 - fp->top;
				else fp->len += amount;
			} else fp->len += after;            /* 3: holds line2 too */
		} else if (amount == MAXL) {            /* 5: holds line2 only */
			adjust(&fp->nested, 0, line2 - fp->top, amount, after + (fp->top - top), insert);
			fp->len -= line2 - fp->top + 1;
			fp->top = line1;
		} else {
			adjust(&fp->nested, 0, line2 - fp->top, amount, after - amount, insert);
			fp->len += after - amount;
			fp->top += amount;
		}
	}
}

/* foldSplit(): fold i, which starts above top and ends below bot, becomes
   one ending above top and one starting below bot; nested folds between
   are gone already */
static void split(Arr *a, int i, long top, long bot)
{
	ins(a, i + 1);
	Nd *fp = a->d + i, *fp2;
	fp[1].top = bot + 1;
	fp[1].len = fp->len - (fp[1].top - fp->top);
	fp[1].flags = fp->flags;
	Arr *a1 = &fp->nested, *a2 = &fp[1].nested;
	find(a1, bot + 1 - fp->top, &fp2);
	int len = fp2 ? (int)(end(a1) - fp2) : 0;
	if (len > 0) {
		grow(a2, len);
		for (int k = 0; k < len; k++) {
			a2->d[k] = fp2[k];
			a2->d[k].top -= fp[1].top - fp->top;
		}
		a2->n = len;
		a1->n -= len;
	}
	fp->len = top - fp->top;
}

/* foldRemove(): no folds in [top, bot] */
static void remove_(Arr *a, long top, long bot)
{
	if (bot < top) return;
	Nd *fp = NULL;
	while (a->n) {
		if (find(a, top, &fp) && fp->top < top) {   /* 2, 3 */
			remove_(&fp->nested, top - fp->top, bot - fp->top);
			if (fp->top + fp->len - 1 > bot) split(a, (int)(fp - a->d), top, bot);
			else fp->len = top - fp->top;
			continue;
		}
		if (fp >= end(a) || fp->top > bot) break;   /* 6 */
		if (fp->top + fp->len - 1 > bot) {          /* 5 */
			adjust(&fp->nested, 0, bot - fp->top, MAXL, fp->top - bot - 1, 0);
			fp->len -= bot - fp->top + 1;
			fp->top = bot + 1;
			break;
		}
		del(a, (int)(fp - a->d));                    /* 4 */
	}
}

/* foldMerge(): fp2 in a ends right below fp1 */
static void merge(Nd *fp1, Arr *a, Nd *fp2)
{
	Arr *a1 = &fp1->nested, *a2 = &fp2->nested;
	Nd *fp3, *fp4;
	if (find(a1, fp1->len - 1, &fp3) && find(a2, 0, &fp4)) merge(fp3, a2, fp4);
	if (a2->n) {
		grow(a1, a2->n);
		for (int k = 0; k < a2->n; k++) {
			a1->d[a1->n] = a2->d[k];
			a1->d[a1->n++].top += fp1->len;
		}
		a2->n = 0;
	}
	fp1->len += fp2->len;
	del(a, (int)(fp2 - a->d));
}

/* foldlevelExpr() for "n", ">n" and "-1" (undefined: the folds take the
   level of the next defined line; 0 on the first line) */
static int level(const Fold *f, Line *l)
{
	long lnum = l->lnum + l->off;
	l->start = 0;
	l->lvl = f->expr(f->ctx, (size_t)(lnum - 1), &l->start);
	if (l->lvl < 0 && lnum <= 1) l->lvl = 0;
	return l->lvl;
}

/* foldUpdateIEMSRecurse(): the fold at level that starts at l->lnum */
static long recurse(Fold *f, Arr *a, int lvl0, long startlnum, Line *l, long bot, char topflags)
{
	Nd *fp = NULL, *fp2;
	int lvl = lvl0, finish = 0;
	long startlnum2 = startlnum, firstlnum = l->lnum, linecount = l->n - l->off;
	l->lnum_save = l->lnum;
	for (;;) {
		lvl = l->lvl < MAX_LEVEL ? l->lvl : MAX_LEVEL;
		if (l->lnum > firstlnum && lvl0 > lvl - l->start) lvl = 0;
		if (l->lnum > bot && !finish && fp) {
			/* a change removed a nested fold, or made this one go on */
			int i = 0;
			fp2 = fp;
			if (lvl >= lvl0)
				for (long ll = l->lnum - fp->top; find(&fp2->nested, ll, &fp2); ll -= fp2->top) i++;
			if (lvl < lvl0 + i) {
				/* when no nested fold holds the line, Neovim reads the
				   slot past the last one: zero, or what a deletion or a
				   split left there (beyond the allocation: zero here) */
				find(&fp->nested, l->lnum - fp->top, &fp2);
				if (fp2) {
					Nd z = {0}, *p = fp2 - fp->nested.d < fp->nested.cap ? fp2 : &z;
					bot = p->top + p->len - 1 + fp->top;
				}
			} else if (fp->top + fp->len <= l->lnum && lvl >= lvl0) finish = 1;
			else break;
		}
		if (!fp && (lvl != lvl0 || l->lnum_save >= bot || l->start || l->lnum == linecount)) {
			/* remove or update the folds between startlnum and firstlnum */
			for (;;) {
				int concat = !l->start;
				if (a->n && (find(a, startlnum, &fp)
				             || (fp < end(a) && fp->top <= firstlnum)
				             || find(a, firstlnum - concat, &fp)
				             || (fp < end(a) && ((lvl < lvl0 && fp->top < l->lnum)
				                                 || (lvl >= lvl0 && fp->top <= l->lnum_save))))) {
					if (fp->top + fp->len + concat > firstlnum) {
						/* reuse it, moving its start to firstlnum */
						if (fp->top == firstlnum) {
						} else if (fp->top >= startlnum) {
							if (fp->top > firstlnum)
								adjust(&fp->nested, 0, MAXL, fp->top - firstlnum, 0, 0);
							else
								adjust(&fp->nested, 0, firstlnum - fp->top - 1, MAXL, fp->top - firstlnum, 0);
							fp->len += fp->top - firstlnum;
							fp->top = firstlnum;
						} else if ((l->start && lvl == lvl0) || firstlnum != startlnum) {
							/* a break in a fold that began above startlnum */
							long bs = firstlnum != startlnum ? startlnum : l->lnum;
							long be = firstlnum != startlnum ? firstlnum : l->lnum;
							remove_(&fp->nested, bs - fp->top, be - fp->top);
							int i = (int)(fp - a->d);
							split(a, i, bs, be - 1);
							fp = a->d + i + 1;
							finish = 1;
						}
						if (fp->top == startlnum && concat && fp > a->d && fp[-1].top + fp[-1].len == fp->top) {
							merge(fp - 1, a, fp);
							fp--;
						}
						break;
					}
					if (fp->top >= startlnum) del(a, (int)(fp - a->d));
					else {   /* truncate above startlnum */
						fp->len = startlnum - fp->top;
						adjust(&fp->nested, fp->len, MAXL, MAXL, 0, 0);
					}
				} else {
					int i = a->n ? (int)(fp - a->d) : 0;
					ins(a, i);
					fp = a->d + i;
					fp->top = firstlnum;
					fp->len = bot - firstlnum + 1;
					/* open in an open fold, else as the fold above it or the
					   one holding it */
					fp->flags = topflags == FD_OPEN || i <= 0 ? topflags : fp[-1].flags;
					finish = 1;
					break;
				}
			}
		}
		if (lvl < lvl0 || l->lnum > linecount) break;
		if (lvl > lvl0 && fp) {   /* a nested fold */
			if (bot < l->lnum) bot = l->lnum;
			l->lnum = l->lnum_save - fp->top;
			l->off += fp->top;
			bot = recurse(f, &fp->nested, lvl0 + 1, startlnum2 - fp->top, l, bot - fp->top, fp->flags);
			l->lnum += fp->top;
			l->lnum_save += fp->top;
			l->off -= fp->top;
			bot += fp->top;
			startlnum2 = l->lnum;
		} else {
			/* past undefined lines to the level after them */
			long ll = l->lnum_save + 1;
			for (l->lnum = ll; l->lnum <= linecount && level(f, l) < 0; l->lnum++);
			if (l->lnum > linecount) break;
			l->lnum_save = l->lnum;
			l->lnum = ll;
		}
	}
	if (!fp) return bot;   /* only undefined lines ("-1") followed, to the end */
	if (fp->len < l->lnum - fp->top) fp->len = l->lnum - fp->top;
	else if (fp->top + fp->len > linecount) fp->len = linecount - fp->top + 1;
	remove_(&fp->nested, startlnum2 - fp->top, l->lnum - 1 - fp->top);
	if (lvl < lvl0 && fp->len != l->lnum - fp->top) {
		/* it ends above l->lnum: the lines it held below bot again */
		if (fp->top + fp->len - 1 > bot) bot = fp->top + fp->len - 1;
		fp->len = l->lnum - fp->top;
	}
	/* following folds that end before l->lnum go */
	for (;;) {
		fp2 = fp + 1;
		if (fp2 >= end(a) || fp2->top > l->lnum) break;
		if (fp2->top + fp2->len > l->lnum) {
			if (fp2->top < l->lnum) {
				adjust(&fp2->nested, 0, l->lnum - fp2->top - 1, MAXL, fp2->top - l->lnum, 0);
				fp2->len -= l->lnum - fp2->top;
				fp2->top = l->lnum;
			}
			if (lvl >= lvl0) merge(fp, a, fp2);
			break;
		}
		del(a, (int)(fp2 - a->d));
	}
	return bot > l->lnum - 1 ? bot : l->lnum - 1;
}

/* foldUpdateIEMS() */
static void update(Fold *f, long top, long bot, long n)
{
	if (f->invalid) { top = 1; bot = n; f->invalid = 0; }
	if (top > n) top = n;
	/* back to a line with a defined level, from the one above top */
	Line l = { .lnum = top > 1 ? top - 1 : top, .n = n };
	while (level(f, &l) < 0) l.lnum--;
	long start = l.lnum, stop = bot;
	if (start > stop && stop < n) stop = start;
	Nd *fp;
	while (l.lnum <= n) {
		if (l.lnum > stop) {
			/* a fold the change removed: on to where it ended */
			if ((start <= stop && find(&f->folds, stop, &fp) && fp->top + fp->len - 1 > stop)
			    || (!l.lvl && find(&f->folds, l.lnum, &fp) && fp->top < l.lnum))
				stop = fp->top + fp->len - 1;
			else break;
		}
		if (l.lvl > 0) {
			stop = recurse(f, &f->folds, 1, start, &l, stop, FD_LEVEL);
			start = l.lnum;
		} else {
			if (l.lnum == n) break;
			l.lnum++;
			level(f, &l);
		}
	}
	remove_(&f->folds, start, stop);
}

Fold *fold_new(int lvl, FoldExpr *expr, FoldTall *tall, void *ctx)
{
	Fold *f = malloc(sizeof *f);
	if (f) *f = (Fold){ .level = lvl, .invalid = 1, .expr = expr, .tall = tall, .ctx = ctx };
	return f;
}

static void copy(Arr *to, const Arr *from)   /* cloneFoldGrowArray() */
{
	grow(to, from->n);
	for (int i = 0; i < from->n; i++) {
		to->d[i] = from->d[i];
		to->d[i].nested = (Arr){0};
		to->n = i + 1;
		copy(&to->d[i].nested, &from->d[i].nested);
	}
}

Fold *fold_dup(const Fold *f, void *ctx)
{
	Fold *g = fold_new(f->level, f->expr, f->tall, ctx);
	if (!g) return NULL;
	g->invalid = f->invalid;
	g->n = f->n;
	copy(&g->folds, &f->folds);
	return g;
}

void fold_free(Fold *f)
{
	if (!f) return;
	clear(&f->folds);
	free(f);
}

void fold_adjust(Fold *f, size_t l1, size_t l2, long amount, long after, int insert)
{
	long line1 = (long)l1 + 1, line2 = l2 == (size_t)-1 ? MAXL : (long)l2;
	if (line2 < line1 && !after) return;   /* mark_adjust_buf(): nothing to do */
	/* lines after the range move by after, so the count changes by it;
	   with no lines after it, by what the range moved */
	f->n += l2 == (size_t)-1 ? amount : after;
	if (amount == FOLD_DEL) amount = MAXL;
	/* deleting some of the lines: only their folds go */
	if (amount == MAXL && line2 >= line1 && line2 - line1 >= -after) line2 = line1 - after - 1;
	if (line2 < line1) line2 = line1;
	/* a line appended in Insert mode joins the fold above it */
	if (insert && amount == 1 && line2 == MAXL) line1--;
	adjust(&f->folds, line1, line2, amount, after, insert);
}

void fold_update(Fold *f, size_t top, size_t bot, size_t n)
{
	f->n = (long)n;
	if (n)   /* an empty buffer has no folds: Neovim's never has no lines */
		update(f, (long)top + 1, (long)bot, (long)n);
}

/* check_closed() with checkSmall() for 'foldminlines' 1: a fold of one
   line that takes one screen row shows open.  Neovim caches that in the
   fold (fd_small) and misses some changes that make it stale; this asks
   each time (tests/test_fold.lua, the known difference) */
static int closed(const Fold *f, const Nd *fp, int *use_level, int lvl, long off)
{
	int c;
	if (*use_level || fp->flags == FD_LEVEL) { *use_level = 1; c = lvl >= f->level; }
	else c = fp->flags == FD_CLOSED;
	return c && (fp->len > 1 || (f->tall && f->tall(f->ctx, (size_t)(fp->top + off - 1))));
}

int fold_closed(const Fold *f, size_t line, size_t *l1, size_t *l2)
{
	long lnum = (long)line + 1, rel = lnum, first = 0;
	int lvl = 0, use_level = 0;
	const Arr *a = &f->folds;
	Nd *fp;
	for (; find(a, rel, &fp); a = &fp->nested, rel -= fp->top, lvl++) {
		first += fp->top;
		if (closed(f, fp, &use_level, lvl, lnum - rel)) {
			*l1 = (size_t)first - 1;
			long last = first + fp->len - 1;
			*l2 = (size_t)(last < f->n ? last : f->n) - 1;
			return lvl + 1;   /* fold_info()'s fi_level, for foldtext's dashes */
		}
	}
	return 0;
}

int fold_level(const Fold *f, size_t line)   /* foldLevelWin() */
{
	long rel = (long)line + 1;
	int lvl = 0;
	Nd *fp;
	for (const Arr *a = &f->folds; find(a, rel, &fp); a = &fp->nested, rel -= fp->top) lvl++;
	return lvl;
}

static void open_nested(Nd *fp)   /* foldOpenNested() */
{
	for (int i = 0; i < fp->nested.n; i++) {
		open_nested(&fp->nested.d[i]);
		fp->nested.d[i].flags = FD_OPEN;
	}
}

enum { DONE_ACTION = 1, DONE_FOLD = 2 };

/* setManualFoldWin() */
static int manual(Fold *f, long lnum, int opening, int recurse)
{
	Nd *fp, *found = NULL;
	int lvl = 0, done = 0;
	for (Arr *a = &f->folds; find(a, lnum, &fp); a = &fp->nested, lnum -= fp->top, lvl++) {
		done = DONE_FOLD;
		/* from 'foldlevel' to manual */
		if (fp->flags == FD_LEVEL) {
			fp->flags = lvl >= f->level ? FD_CLOSED : FD_OPEN;
			for (int j = 0; j < fp->nested.n; j++) fp->nested.d[j].flags = FD_LEVEL;
		}
		if (fp->flags == FD_CLOSED) {   /* opening opens the topmost closed one */
			if (opening) {
				fp->flags = FD_OPEN;
				done |= DONE_ACTION;
				if (recurse) open_nested(fp);
			}
			break;
		}
		found = fp;
	}
	/* closing closes the deepest open one */
	if (!opening && found) { found->flags = FD_CLOSED; done |= DONE_ACTION; }
	return done;
}

static int deepest(const Arr *a)   /* getDeepestNestingRecurse() */
{
	int m = 0;
	for (int i = 0; i < a->n; i++) {
		int d = deepest(&a->d[i].nested) + 1;
		if (d > m) m = d;
	}
	return m;
}

int fold_cmd(Fold *f, int op, size_t line, long count)
{
	long lnum = (long)line + 1;
	size_t l1, l2;
	switch (op) {
	case FOLD_TOGGLE:
		op = fold_closed(f, line, &l1, &l2) ? FOLD_OPEN : FOLD_CLOSE;
		/* fall through */
	case FOLD_OPEN:
	case FOLD_CLOSE:   /* setFoldRepeat(); zO's second round finds nothing closed */
	case FOLD_OPEN_REC:
		for (long n = 0; n < count; n++) {
			int done = manual(f, lnum, op != FOLD_CLOSE, op == FOLD_OPEN_REC);
			if (!(done & DONE_ACTION)) return n ? 1 : done ? 0 : -1;
		}
		return count > 0;
	case FOLD_VIEW: {   /* foldOpenCursor() */
		int done = 0;
		while (manual(f, lnum, 1, 0) & DONE_ACTION) done = 1;
		return done;
	}
	case FOLD_OPEN_ALL: count = deepest(&f->folds); break;
	case FOLD_CLOSE_ALL: count = 0; break;
	case FOLD_RESET: count = f->level; break;
	}
	/* newFoldLevelWin() */
	f->level = (int)count;
	for (int i = 0; i < f->folds.n; i++) f->folds.d[i].flags = FD_LEVEL;
	return 1;   /* newFoldLevel() always calls changed_window_setting() */
}
