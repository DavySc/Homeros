/* Lines, positions and edits for the keys, in Neovim's terms (memline.c,
   misc1.c inc()/dec(), cursor.c, edit.c cursor_down()/oneright(), indent.c). */
#include <string.h>
#include "k.h"
#include "uni.h"


int set_curswant;
int beeped;

/* A few lines kept as NUL-terminated copies: Neovim's code holds two or
   three line pointers at once.  gen changes with every edit keys makes and
   with every key fed (the text may have changed in between: a reload, the
   core's undo); a copy is also only good for its buffer (CTRL-W w). */
static unsigned long gen = 1;
static struct { const Buf *b; long l; unsigned long gen; char *s; size_t cap; long n; } ring[4];
static int ring_next;

long nlines(void) { return (long)core_lines(E); }

static int slot(long l)
{
	for (int i = 0; i < 4; i++)
		if (ring[i].gen == gen && ring[i].l == l && ring[i].b == B) return i;
	int i = ring_next;
	ring_next = (ring_next + 1) % 4;
	size_t n;
	const unsigned char *s = core_line(E, (size_t)l, &n);
	if (n + 1 > ring[i].cap) {
		/* doubling: a line growing by a few bytes per key (a count on an
		   insert) would otherwise reallocate its whole copy every key */
		size_t cap = ring[i].cap * 2 > n + 1 ? ring[i].cap * 2 : n + 1;
		char *b = realloc(ring[i].s, cap);
		if (!b) { ring[i].n = 0; ring[i].gen = 0; static char z; return (ring[i].s = &z, i); }
		ring[i].s = b;
		ring[i].cap = cap;
	}
	memcpy(ring[i].s, s, n);
	ring[i].s[n] = 0;
	ring[i].n = (long)n;
	ring[i].b = B;
	ring[i].l = l;
	ring[i].gen = gen;
	return i;
}

const char *ml_get(long l) { return ring[slot(l)].s; }
long ml_len(long l) { return ring[slot(l)].n; }
int lineempty(long l) { return ml_len(l) == 0; }

/* Neovim 0.12 steps by extended grapheme cluster (utfc_ptr2len(),
   utf_head_off()): an accent, a flag or a ZWJ emoji is one character */
int clen(const char *s, long n, long i)
{
	return i < n ? (int)uni_glen((const unsigned char *)s, (size_t)n, (size_t)i) : 1;
}

long head(const char *s, long i)
{
	if (i <= 0) return 0;
	long n = (long)strlen(s);
	if (i >= n) return i;
	return (long)uni_gprev((const unsigned char *)s, (size_t)n, (size_t)i + 1);   /* the cluster holding byte i */
}

/* utf_ptr2char(): an invalid byte stands for itself */
static int char_at(const char *s, long n, long i)
{
	if (i >= n) return 0;
	unsigned char b = (unsigned char)s[i];
	if (b < 0x80) return b;
	if (uni_len((const unsigned char *)s, (size_t)n, (size_t)i) == 1) return b;
	return (int)uni_cp((const unsigned char *)s, (size_t)n, (size_t)i);
}

int gchar(Pos p)
{
	int i = slot(p.lnum);
	return char_at(ring[i].s, ring[i].n, p.col);
}

/* the cursor's coladd: the core's cursor is line and byte, so keys keeps
   the cells past it, which only a Visual block uses */
static long coladd;
Pos cur(void) { return (Pos){ (long)W->line, (long)W->col, coladd }; }
void setcur(Pos p) { W->line = (size_t)p.lnum; W->col = (size_t)p.col; coladd = p.coladd; }

/* virtual_active(): 'virtualedit' is "block" (FEATURES.md), so only in a
   Visual block */
int virtual_active(void) { return virtual_op >= 0 ? virtual_op : VIsual_active && VIsual_mode == 22; }
int gchar_cursor(void) { return gchar(cur()); }
int lt(Pos a, Pos b) { return a.lnum != b.lnum ? a.lnum < b.lnum : a.col != b.col ? a.col < b.col : a.coladd < b.coladd; }
int eq(Pos a, Pos b) { return a.lnum == b.lnum && a.col == b.col && a.coladd == b.coladd; }

int inc(Pos *p)
{
	int i = slot(p->lnum);
	if (p->col < ring[i].n) {
		p->col += clen(ring[i].s, ring[i].n, p->col);
		return p->col < ring[i].n ? 0 : 2;
	}
	if (p->lnum + 1 < nlines()) { p->lnum++; p->col = 0; p->coladd = 0; return 1; }
	return -1;
}

int dec(Pos *p)
{
	p->coladd = 0;
	if (p->col > 0) {
		const char *s = ml_get(p->lnum);
		long n = ml_len(p->lnum);
		p->col = p->col > n ? n : (long)uni_gprev((const unsigned char *)s, (size_t)n, (size_t)p->col);
		return 0;
	}
	if (p->lnum > 0) { p->lnum--; p->col = ml_len(p->lnum); return 1; }
	return -1;
}

int incl(Pos *p) { int r = inc(p); if (r >= 1 && p->col) r = inc(p); return r; }
int decl(Pos *p) { int r = dec(p); if (r == 1 && p->col) r = dec(p); return r; }
int inc_cursor(void) { Pos p = cur(); int r = inc(&p); setcur(p); return r; }
int dec_cursor(void) { Pos p = cur(); int r = dec(&p); setcur(p); return r; }

long vcol_of(long l, long col) { return (long)core_vcol(E, (size_t)l, (size_t)col); }

long vcol_end(long l, long col)
{
	long n = ml_len(l);
	if (col >= n) return vcol_of(l, col);
	return vcol_of(l, col + clen(ml_get(l), n, col)) - 1;
}

long col_of_vcol(long l, long v, int past_end)
{
	return (long)core_col_at(E, (size_t)l, v >= MAXCOL ? WANT_EOL : (size_t)v, past_end);
}

/* the cursor may sit on the end of line in Insert and Visual mode
   (coladvance2()'s one_more, 'selection' inclusive) */
static int one_more(void) { return E->mode == MODE_INSERT || VIsual_active || restart_edit; }

/* the cells of the character at col, as oneright() counts them: a Tab
   or a control character is 1 */
static long cells(long l, long col)
{
	const char *s = ml_get(l);
	unsigned char c = (unsigned char)s[col];
	if (c < 0x20 || c == 0x7f) return 1;
	return vcol_of(l, col + clen(s, ml_len(l), col)) - vcol_of(l, col);
}

/* getvvcol(): with 'virtualedit' a position's columns include coladd, and
   a wide character counts its cells only when coladd is not past it */
void getvvcol(Pos p, long *start, long *end)
{
	long s = vcol_of(p.lnum, p.col), e;
	if (!virtual_active()) { e = vcol_end(p.lnum, p.col); }
	else {
		long add = p.coladd, endadd = 0;
		if (p.col < ml_len(p.lnum) && (unsigned char)ml_get(p.lnum)[p.col] != '\t') {
			endadd = cells(p.lnum, p.col) - 1;
			if (add > endadd) endadd = 0;
			else add = 0;
		}
		s += add;
		e = s + endadd;
	}
	if (start) *start = s;
	if (end) *end = e;
}

long getviscol(void) { long v; getvvcol(cur(), &v, NULL); return v; }

/* coladvance2() without addspaces; finetune (coladd) when virtual_active() */
void coladvance(long want)
{
	long l = (long)W->line;
	W->col = (size_t)col_of_vcol(l, want, one_more());
	coladd = 0;
	if (virtual_active() && want < MAXCOL) {
		long v = vcol_of(l, (long)W->col);
		if (want > v) coladd = want - v;
	}
}

/* coladvance_force(): coladvance2() with addspaces, as 'virtualedit' does
   for an insert at a column past the text: spaces fill up to the column,
   and a wide character or Tab under it becomes spaces */
void coladvance_force(long want)
{
	long l = (long)W->line, n = ml_len(l);
	long idx = col_of_vcol(l, want, 1), col = vcol_of(l, idx);
	coladd = 0;
	if (idx >= n) {
		if (want > col) {
			char *sp = malloc((size_t)(want - col));
			if (!sp) return;
			memset(sp, ' ', (size_t)(want - col));
			replace_bytes(l, n, 0, sp, want - col);
			free(sp);
			idx = n + want - col;
		}
	} else {
		long csize = vcol_of(l, idx + clen(ml_get(l), n, idx)) - col;
		if (col != want || csize > 1) {
			char *sp = malloc((size_t)csize);
			if (!sp) return;
			memset(sp, ' ', (size_t)csize);
			replace_bytes(l, idx, clen(ml_get(l), n, idx), sp, csize);
			free(sp);
			idx += want - col;
		}
	}
	W->col = (size_t)idx;
}

/* validate_virtcol(): w_virtcol as curs_columns() leaves it after each
   command, kept until the cursor moves, also across Visual mode's start and
   end (VALID_VIRTCOL; measured: "v Esc j", "w v h Esc j", "C-v { Esc j" on
   a Tab).  In Visual mode it is getvvcol() of the cursor: with
   'virtualedit' a Tab's first cell plus coladd, and with the cursor at or
   before Visual's start ('selection' inclusive) a Tab's first cell. */
void validate_virtcol(void)
{
	static long coladd;   /* Win.vc has no coladd, which moves the cursor too */
	if (W->vc.ok && W->vc.line == W->line && W->vc.col == W->col && W->vc.mode == E->mode && coladd == cur().coladd) return;
	coladd = cur().coladd;
	long v;
	if (virtual_active()) v = getviscol();
	else if (VIsual_active && !lt(VIsual, cur())) v = vcol_of((long)W->line, (long)W->col);
	else {
		/* core_set_want() fills the cache; on a Tab Neovim's virtcol is
		   its last cell in Normal mode */
		size_t w = W->want;
		core_set_want(E);
		W->want = w;
		return;
	}
	W->vc.line = W->line; W->vc.col = W->col; W->vc.v = (size_t)v;
	W->vc.mode = E->mode; W->vc.ok = 1;
}

void update_curswant(void)
{
	if (!set_curswant) return;
	validate_virtcol();
	W->want = W->vc.v;
	set_curswant = 0;
}

static long curswant(void) { return W->want == WANT_EOL ? MAXCOL : (long)W->want; }

void beginline(int flags)
{
	if (flags & BL_SOL) {     /* 'nostartofline' (FEATURES.md): curswant as it is */
		coladvance(curswant());
		return;
	}
	W->col = 0;
	coladd = 0;
	if (flags & (BL_WHITE | BL_SOL)) {
		const char *s = ml_get((long)W->line);
		long c = 0;
		while ((s[c] == ' ' || s[c] == '\t') && !((flags & BL_FIX) && s[c + 1] == 0)) c++;
		W->col = (size_t)c;
	}
	set_curswant = 1;
}

/* get_indent_str(): counts the blanks byte by byte, so a combining mark on
   the last blank ends the indent ('tabstop' 8) */
long get_indent(long l)
{
	long v = 0;
	for (const char *s = ml_get(l); *s == ' ' || *s == '\t'; s++) v += *s == '\t' ? 8 - v % 8 : 1;
	return v;
}

int linewhite(long l)
{
	const char *s = ml_get(l);
	while (*s == ' ' || *s == '\t') s++;
	return *s == 0;
}

int oneright(void)
{
	if (virtual_active()) {
		Pos prev = cur();
		long l = (long)W->line;
		coladvance(getviscol() + ((long)W->col < ml_len(l) ? cells(l, (long)W->col) : 1));
		set_curswant = 1;
		return !eq(prev, cur());
	}
	long l = (long)W->line, n = ml_len(l), c = (long)W->col;
	if (c >= n) return 0;
	int k = clen(ml_get(l), n, c);
	/* move "l" bytes right, but don't end up on the NUL, unless 'virtualedit'
	   contains "onemore" */
	if (c + k >= n) return 0;
	W->col = (size_t)(c + k);
	set_curswant = 1;
	return 1;
}

int oneleft(void)
{
	if (virtual_active()) {
		long v = getviscol();
		if (v == 0) return 0;
		coladvance(v - 1);
		if (coladd == 1 && (long)W->col < ml_len((long)W->line)
		    && (unsigned char)ml_get((long)W->line)[W->col] != '\t' && cells((long)W->line, (long)W->col) > 1)
			coladd = 0;
		set_curswant = 1;
		return 1;
	}
	if (W->col == 0) return 0;
	set_curswant = 1;
	dec_cursor();
	return 1;
}

int cursor_down(long n)
{
	long l = (long)W->line, nl = nlines();
	if (n > 0 && l + 1 >= nl) return 0;
	W->line = (size_t)(l + n >= nl ? nl - 1 : l + n);
	coladvance(curswant());
	return 1;
}

int cursor_up(long n)
{
	long l = (long)W->line;
	if (n > 0 && l <= 0) return 0;
	W->line = (size_t)(n >= l ? 0 : l - n);
	coladvance(curswant());
	return 1;
}

void check_cursor_col(void)
{
	long l = (long)W->line, n = ml_len(l), c = (long)W->col;
	if (c > n) c = n;
	/* Neovim's check_cursor_col() moves to a head byte only past the end:
	   inside a line the cursor may stay in a cluster (stop_insert() then
	   sees the mark after an autoindent, not its blank) */
	if (c == n && n > 0 && !one_more() && !virtual_active()) c = head(ml_get(l), n - 1);
	W->col = (size_t)c;
}

void check_cursor(void)
{
	if ((long)W->line >= nlines()) W->line = (size_t)nlines() - 1;
	check_cursor_col();
}

/* adjust_cursor_eol(): leaving Insert mode, the cursor steps back onto a
   character */
void adjust_cursor_eol(void)
{
	long l = (long)W->line, n = ml_len(l);
	if ((long)W->col > 0 && (long)W->col >= n && !restart_edit && E->mode != MODE_INSERT) {
		W->col = (size_t)head(ml_get(l), n - 1);
	}
}

static int edited(int r) { gen++; return r; }
void buf_changed(void) { gen++; }

int replace_bytes(long l, long col, long ndel, const char *ins, long nins)
{
	size_t p = core_line_start(E, (size_t)l) + (size_t)col;
	return edited(core_edit(E, p, (size_t)ndel, ins, (size_t)nins));
}

int append_lines(long after, const char *s, long n)
{
	char *b = malloc((size_t)n + 1);
	if (!b) return -1;
	memcpy(b, s, (size_t)n);
	b[n] = '\n';
	/* ml_append(): an empty buffer keeps its one line */
	int r = core_edit(E, core_line_start(E, (size_t)(after + 1)), 0, b, (size_t)n + 1);
	free(b);
	return edited(r);
}

int del_lines(long l, long n)
{
	long nl = nlines();
	if (n <= 0) return 0;
	return edited(core_delete_lines(E, (size_t)l, (size_t)(l + n < nl ? l + n : nl)));
}

int is_empty_buf(void) { return (B->flags & CORE_EMPTY) != 0; }

/* set_indent() with 'expandtab': the indent becomes spaces; no change when
   it is already exactly that */
int set_indent(long l, long cols)
{
	const char *s = ml_get(l);
	long i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	if (i == cols) {
		long k = 0;
		while (k < i && s[k] == ' ') k++;
		if (k == i) return 0;
	}
	char *b = malloc((size_t)cols + 1);
	if (!b) return -1;
	memset(b, ' ', (size_t)cols);
	int r = replace_bytes(l, 0, i, b, cols);
	free(b);
	return r;
}

/* u_save(l - 1, l), which saves no line: an undo step starts at line l
   even if nothing changes.  The core takes a group's lowest byte for its
   first changed line and moves other windows' cursors off the lines it
   replaced, so a line that only gets lines below it must not be in it */
void u_save_gap(size_t l) { core_u_save(E, l); }

/* u_save_cursor() */
void u_save_cursor(void) { u_save_gap(W->line); }
