/* Registers and operators: Neovim 0.12's register.c and ops.c (op_delete(),
   op_yank_reg(), do_put(), op_change(), op_shift(), op_tilde(), do_join(),
   op_replace(), op_insert(), do_addsub()), with FEATURES.md's settings
   ('expandtab', 'shiftwidth' 4, 'nrformats' bin,hex, 'virtualedit' block). */
#include <stdio.h>
#include <string.h>
#include "k.h"
#include "loop.h"
#include "sqlfmt.h"
#include "str.h"
#include "uni.h"

enum { SW = 4 };

Pos b_op_start, b_op_end;      /* the '[ and '] marks */
Pos b_op_start_orig;          /* Insstart_orig of the last insert, for a block I or A */
Reg *put_override;             /* nv_put()'s copy of the register (Visual p) */

/* ---- registers ---- */

/* 0-9, a-z, "-" and "+" (the clipboard's copy, get_clipboard()) */
static Reg y_regs[38];
static Reg *y_previous;
static char *last_cmdline;

static int op_reg_index(int r)
{
	if (r >= '0' && r <= '9') return r - '0';
	if (r >= 'a' && r <= 'z') return r - 'a' + 10;
	if (r >= 'A' && r <= 'Z') return r - 'A' + 10;
	if (r == '-') return 36;
	if (r == '+' || r == '*') return 37;
	return -1;
}

int valid_yank_reg(int r, int writing)
{
	if ((r > 0 && r < 0x80 && ((r >= '0' && r <= '9') || (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z')))
	    || (!writing && has("/.%:", r)) || r == '"' || r == '-' || r == '_' || r == '*' || r == '+')
		return 1;
	return 0;
}

/* get_yank_register() */
static Reg *get_clipboard(int r);
Reg *get_reg(int r, int yank)
{
	Reg *c = yank ? NULL : get_clipboard(r);
	if (c) return c;
	if (!yank && (r == 0 || r == '"') && y_previous) return y_previous;
	int i = op_reg_index(r);
	if (i < 0) i = 0;
	if (yank) y_previous = &y_regs[i];
	return &y_regs[i];
}

static void free_reg(Reg *r) { free(r->s); r->s = NULL; r->n = 0; }

void set_reg(int r, const char *s, long n, int type)
{
	Reg *y = &y_regs[op_reg_index(r) < 0 ? 0 : op_reg_index(r)];
	int append = r >= 'A' && r <= 'Z' && y->s;
	long old = append ? y->n : 0;
	char *b = realloc(append ? y->s : NULL, (size_t)(old + n + 2));
	if (!b) return;
	if (!append) free_reg(y);
	if (append && (y->type == MLINE || type == MLINE) && 0) b[old++] = '\n';
	memcpy(b + old, s, (size_t)n);
	y->s = b;
	y->n = old + n;
	if (!append) y->type = type;
	y->width = 0;
}

void set_last_cmdline(const char *s)
{
	free(last_cmdline);
	last_cmdline = strdup(s);
}

/* the lines of a register: s[0..n) split at '\n' */
static long reg_lines(const Reg *r)
{
	if (!r->s) return 0;
	long k = 1;
	for (long i = 0; i < r->n; i++) k += r->s[i] == '\n';
	return k;
}

static const char *reg_line(const Reg *r, long i, long *len)
{
	const char *p = r->s, *e = r->s + r->n;
	while (i-- > 0) p = memchr(p, '\n', (size_t)(e - p)) + 1;
	const char *q = memchr(p, '\n', (size_t)(e - p));
	*len = (q ? q : e) - p;
	return p;
}

void bput(Sbuf *b, const char *s, long n)
{
	if (b->n + n + 1 > b->cap) {
		long c = (b->n + n + 1) * 2;
		char *t = realloc(b->s, (size_t)c);
		if (!t) return;
		b->s = t;
		b->cap = c;
	}
	memcpy(b->s + b->n, s, (size_t)n);
	b->n += n;
	b->s[b->n] = 0;
}
static void bspaces(Sbuf *b, long n) { while (n-- > 0) bput(b, " ", 1); }

/* The system clipboard, with 'clipboard' unnamedplus (Davy, 2026-11-18):
   Neovim's clipboard.c, set_clipboard() after a yank or delete and
   get_clipboard() before a put, for "+ "* and the unnamed register.  "+
   doubles as the provider's cache (clipboard.vim's s:selections), so a
   text read back as it was set keeps its type (a block, lines); other text
   is lines when it ends in a line break. */
static void clip_text(const Reg *r, Sbuf *b)
{
	bput(b, "", 0);
	bput(b, r->s ? r->s : "", r->n);
	if (r->type != MCHAR) bput(b, "\n", 1);
}

static void set_clipboard(int r, const Reg *y)
{
	if (!E->clip_set || (r && r != '+' && r != '*')) return;
	Sbuf b = { 0 };
	clip_text(y, &b);
	if (!E->clip_set(b.s, (size_t)b.n) && y != &y_regs[37]) {
		set_reg('+', y->s ? y->s : "", y->n, y->type);
		y_regs[37].width = y->width;
	}
	free(b.s);
}

static Reg *get_clipboard(int r)
{
	if (!E->clip_get || (r && r != '+' && r != '*')) return NULL;
	long n = 4096, m;
	char *s = malloc((size_t)n);
	while (s && (m = E->clip_get(s, (size_t)n)) > n) {
		char *t = realloc(s, (size_t)m);
		if (!t) { free(s); s = NULL; }
		else { s = t; n = m; }
	}
	if (!s || m < 0) { free(s); return NULL; }
	Sbuf b = { 0 };
	clip_text(&y_regs[37], &b);
	if (b.n != m || memcmp(b.s, s, (size_t)m)) {
		int line = m && s[m - 1] == '\n';
		set_reg('+', s, m - line, line ? MLINE : MCHAR);
	}
	free(b.s);
	free(s);
	return &y_regs[37];
}

/* ---- block_prep() ---- */

typedef struct {
	long startspaces, endspaces, textlen, textcol, start_vcol, end_vcol;
	long start_char_vcols, end_char_vcols, pre_whitesp, pre_whitesp_c;
	int is_short, is_oneChar, is_MAX;
} Block;

/* while an operator runs (and the insert it starts) virtual_active() is
   this, as VIsual_active is already reset: 1 for a Visual block; -1
   (kNone) outside operators */
int virtual_op = -1;

/* The walks of block_prep() and do_put(): from byte from, step whole
   characters while the column is below target.  Returns where that stops,
   with *prev the last character stepped and *incr its cells (both kept if
   none was), in a few core lookups instead of one per character, so a long
   line costs its length, not its length squared. */
static long walk(long l, const char *s, long len, long from, long target, long *prev, long *incr)
{
	if (from >= len || vcol_of(l, from) >= target) return from;
	long p = col_of_vcol(l, target, 1), i;
	if (p < len && vcol_of(l, p) < target) i = p + clen(s, len, p);   /* a character spans target */
	else i = p < len ? p : len;
	*prev = head(s, i - 1);
	*incr = vcol_of(l, i) - vcol_of(l, *prev);
	return i;
}

static void bprep(Oparg *oap, Block *bd, long l, int is_del)
{
	long incr = 0;
	int is_MAX = bd->is_MAX;     /* block_prep() leaves is_MAX to its caller */
	memset(bd, 0, sizeof *bd);
	bd->is_MAX = is_MAX;
	const char *line = ml_get(l);
	long len = ml_len(l), prev_pstart = 0;
	long i = walk(l, line, len, 0, oap->start_vcol, &prev_pstart, &incr), vcol = vcol_of(l, i);
	/* the blanks the walk stepped over last (a character that starts with
	   one counts, as Neovim tests its first byte) */
	long b0 = i;
	for (long c; b0 > 0 && (line[c = head(line, b0 - 1)] == ' ' || line[c] == '\t'); b0 = c) bd->pre_whitesp_c++;
	bd->pre_whitesp = vcol - vcol_of(l, b0);
	bd->start_vcol = vcol;
	long pstart = i;
	bd->start_char_vcols = incr;
	if (bd->start_vcol < oap->start_vcol) {
		bd->end_vcol = bd->start_vcol;
		bd->is_short = 1;
		if (!is_del || oap->op_type == OP_APPEND) bd->endspaces = oap->end_vcol - oap->start_vcol + 1;
	} else {
		bd->startspaces = bd->start_vcol - oap->start_vcol;
		if (is_del && bd->startspaces) bd->startspaces = bd->start_char_vcols - bd->startspaces;
		long pend = pstart;
		bd->end_vcol = bd->start_vcol;
		if (bd->end_vcol > oap->end_vcol) {
			bd->is_oneChar = 1;
			if (oap->op_type == OP_INSERT) bd->endspaces = bd->start_char_vcols - bd->startspaces;
			else if (oap->op_type == OP_APPEND) {
				bd->startspaces += oap->end_vcol - oap->start_vcol + 1;
				bd->endspaces = bd->start_char_vcols - bd->startspaces;
			} else {
				bd->startspaces = oap->end_vcol - oap->start_vcol + 1;
				if (is_del && oap->op_type != OP_LSHIFT) {
					bd->startspaces = bd->start_char_vcols - (bd->start_vcol - oap->start_vcol);
					bd->endspaces = bd->end_vcol - oap->end_vcol - 1;
				}
			}
		} else {
			long prev_pend = pend;
			i = walk(l, line, len, pend, oap->end_vcol + 1, &prev_pend, &incr);
			vcol = i == pend ? bd->end_vcol : vcol_of(l, i);
			bd->end_vcol = vcol;
			pend = i;
			if (bd->end_vcol <= oap->end_vcol && (!is_del || oap->op_type == OP_APPEND || oap->op_type == OP_REPLACE)) {
				bd->is_short = 1;
				if (oap->op_type == OP_APPEND || virtual_op)
					bd->endspaces = oap->end_vcol - bd->end_vcol + oap->inclusive;
			} else if (bd->end_vcol > oap->end_vcol) {
				bd->endspaces = bd->end_vcol - oap->end_vcol - 1;
				if (!is_del && bd->endspaces) {
					bd->endspaces = incr - bd->endspaces;
					if (pend != pstart) pend = prev_pend;
				}
			}
		}
		bd->end_char_vcols = incr;
		if (is_del && bd->startspaces) pstart = prev_pstart;
		bd->textlen = pend - pstart;
	}
	bd->textcol = pstart;
}

void block_prep(Oparg *oap, long l, int is_del, long *textcol, long *textlen,
                long *startspaces, long *endspaces, int *is_short, long *start_vcol, long *end_vcol)
{
	Block bd = { 0 };
	bprep(oap, &bd, l, is_del);
	*textcol = bd.textcol; *textlen = bd.textlen; *startspaces = bd.startspaces;
	*endspaces = bd.endspaces; *is_short = bd.is_short; *start_vcol = bd.start_vcol; *end_vcol = bd.end_vcol;
}

/* ---- yank ---- */

/* op_yank_reg(): the text of the operated region */
static void yank_into(Oparg *oap, Reg *reg, int append)
{
	int type = oap->motion_type;
	long yanklines = oap->line_count, endl = oap->end.lnum;
	if (type == MCHAR && oap->start.col == 0 && !oap->inclusive && !oap->is_visual && oap->end.col == 0 && yanklines > 1) {
		type = MLINE;
		endl--;
		yanklines--;
	}
	Sbuf b = { 0 };
	bput(&b, "", 0);
	for (long l = oap->start.lnum; l <= endl; l++) {
		if (l > oap->start.lnum) bput(&b, "\n", 1);
		const char *s = ml_get(l);
		long len = ml_len(l);
		if (type == MBLOCK) {
			Block bd = { 0 };
			bprep(oap, &bd, l, 0);
			bspaces(&b, bd.startspaces);
			bput(&b, s + bd.textcol, bd.textlen);
			bspaces(&b, bd.endspaces);
		} else if (type == MLINE) bput(&b, s, len);
		else {
			long sc = l == oap->start.lnum ? oap->start.col : 0;
			long ec = l == oap->end.lnum ? oap->end.col : len;
			if (ec > len) ec = len;
			long tl = sc > ec ? 0 : ec - sc + (l == oap->end.lnum ? oap->inclusive : 0);
			if (sc + tl > len) tl = len - sc;
			if (tl < 0) tl = 0;
			bput(&b, s + (sc <= len ? sc : 0), tl);
		}
	}
	if (append && reg->s) {
		/* appending: a charwise register joins the first new line to its last */
		Sbuf a = { 0 };
		bput(&a, reg->s, reg->n);
		if (type == MLINE || reg->type != MCHAR) bput(&a, "\n", 1);
		bput(&a, b.s, b.n);
		if (type == MLINE) reg->type = MLINE;
		free(reg->s);
		free(b.s);
		reg->s = a.s;
		reg->n = a.n;
	} else {
		free(reg->s);
		reg->s = b.s;
		reg->n = b.n;
		reg->type = type;
		reg->width = 0;
		if (type == MBLOCK) {
			reg->width = oap->end_vcol - oap->start_vcol;
			if (W->want == WANT_EOL && reg->width > 0) reg->width--;
		}
	}
	b_op_start = oap->start;
	b_op_end = oap->end;
	if (type == MLINE) { b_op_start.col = 0; b_op_end.col = MAXCOL; }
	if (type != MLINE && !oap->inclusive && b_op_end.col > 0) b_op_end.col--;
}

static void unflash(void *e)
{
	((Editor *)e)->ks.yank.type = 0;
	core_changed = 1;
}

int op_yank(Oparg *oap)
{
	if (oap->regname == '_') return 1;
	if (oap->regname && !valid_yank_reg(oap->regname, 1)) { beep(); return 0; }
	int r = oap->regname;
	Reg *reg = get_reg(r, 1);
	yank_into(oap, reg, r >= 'A' && r <= 'Z');
	set_clipboard(r, reg);
	/* Davy's vim.highlight.on_yank(): '[ to '] for 150 ms, not while a
	   register executes */
	if (!E->ks.executing) {
		int t = reg->type == MLINE ? 'V' : reg->type == MBLOCK ? 22 : 'v';
		E->ks.yank.type = t;
		E->ks.yank.l1 = (size_t)b_op_start.lnum, E->ks.yank.c1 = (size_t)b_op_start.col;
		E->ks.yank.l2 = (size_t)b_op_end.lnum, E->ks.yank.c2 = (size_t)b_op_end.col;
		if (t == 22) {   /* screen columns: the register's width from the left one (the core's contract) */
			long a, b;   /* getregionpos(): with coladd, so on a Tab where the cursor shows */
			getvvcol(b_op_start, &a, NULL), getvvcol(b_op_end, &b, NULL);
			E->ks.yank.c1 = (size_t)(a < b ? a : b), E->ks.yank.c2 = E->ks.yank.c1 + (size_t)reg->width;
		}
		loop_timer(unflash, E, 150);
	}
	return 1;
}

static void shift_delete_registers(int y_append)
{
	free(y_regs[9].s);
	for (int n = 9; n > 1; n--) y_regs[n] = y_regs[n - 1];
	if (!y_append) y_previous = &y_regs[1];
	y_regs[1].s = NULL;
	y_regs[1].n = 0;
}

/* ---- delete ---- */

static long line_end_of(Oparg *oap) { return oap->end.col + (oap->inclusive && oap->end.col < ml_len(oap->end.lnum) ? clen(ml_get(oap->end.lnum), ml_len(oap->end.lnum), oap->end.col) : 0); }

int op_delete(Oparg *oap)
{
	extern int did_ai_flag;
	if (is_empty_buf()) return 1;   /* op_delete() returns early on ML_EMPTY */
	if (oap->empty) { u_save_cursor(); return 1; }
	/* mb_adjust_opend() */
	if (oap->motion_type == MCHAR && !oap->is_visual && oap->line_count > 1 && oap->motion_force == 0
	    && oap->op_type == OP_DELETE) {
		const char *p = ml_get(oap->end.lnum) + oap->end.col;
		if (*p) p += oap->inclusive;
		while (*p == ' ' || *p == '\t') p++;
		const char *s = ml_get((long)W->line);
		long i = 0;
		while (s[i] == ' ' || s[i] == '\t') i++;
		if (*p == 0 && i >= (long)W->col) oap->motion_type = MLINE;
	}
	if (oap->motion_type != MLINE && oap->line_count == 1 && oap->op_type == OP_DELETE && ml_len(oap->start.lnum) == 0) {
		/* "D" in an empty line (diw, a block there) deletes and yanks
		   nothing and, unlike the empty region above, saves no undo;
		   with virtual_op only the '[ '] marks are set */
		if (virtual_active()) goto setmarks;
		return 1;
	}
	if (oap->regname != '_') {
		Reg *reg = NULL;
		int did_yank = 0;
		if (oap->regname != 0) {
			if (!valid_yank_reg(oap->regname, 1)) { beep(); return 1; }
			reg = get_reg(oap->regname, 1);
			yank_into(oap, reg, oap->regname >= 'A' && oap->regname <= 'Z');
			did_yank = 1;
		}
		if (oap->motion_type == MLINE || oap->line_count > 1 || oap->use_reg_one) {
			shift_delete_registers(oap->regname >= 'A' && oap->regname <= 'Z');
			reg = &y_regs[1];
			yank_into(oap, reg, 0);
			did_yank = 1;
		}
		if (oap->regname == 0 && oap->motion_type != MLINE && oap->line_count == 1) {
			reg = get_reg('-', 1);
			yank_into(oap, reg, 0);
			did_yank = 1;
		}
		if (did_yank) set_clipboard(oap->regname, reg);
	}
	if (oap->motion_type == MBLOCK) {
		u_save_cursor();
		for (long l = (long)W->line; l <= oap->end.lnum; l++) {
			Block bd = { 0 };
			bprep(oap, &bd, l, 1);
			if (bd.textlen == 0) continue;
			if (l == (long)W->line) setcur((Pos){ l, bd.textcol + bd.startspaces, 0 });
			char sp[64];
			long ns = bd.startspaces + bd.endspaces;
			memset(sp, ' ', sizeof sp);
			replace_bytes(l, bd.textcol, bd.textlen, sp, ns < 64 ? ns : 64);
		}
		check_cursor_col();
		oap->line_count = 0;
	} else if (oap->motion_type == MLINE) {
		if (oap->op_type == OP_CHANGE) {
			/* op_delete(): Neovim deletes the lines with the cursor one line
			   down, so undo saves that line as its cursor (package 4 traced it) */
			if (oap->line_count > 1) {
				size_t l0 = W->line;
				W->line++;
				del_lines((long)l0 + 1, oap->line_count - 1);
				W->line = l0;
			}
			u_save_cursor();
			/* 'autoindent': keep the indent, which goes if nothing is typed */
			beginline(BL_WHITE);
			did_ai_flag = 1;
			long l = (long)W->line, c = (long)W->col, n = ml_len(l);
			if (n > c) replace_bytes(l, c, n - c, "", 0);
		} else {
			del_lines((long)W->line, oap->line_count);
			if ((long)W->line >= nlines()) W->line = (size_t)nlines() - 1;
			beginline(BL_WHITE | BL_FIX);
		}
	} else {
		if (oap->line_count == 1) {
			u_save_cursor();
			long l = oap->start.lnum, c = oap->start.col, n = line_end_of(oap) - c;
			/* del_bytes() with fixpos backs up off the last character
			   (not in a CTRL-O command: restart_edit) before the edit, so
			   the core sees the cursor where it ends (the view keeps its
			   top line: PR 20's review, rg1_84) */
			if (oap->op_type == OP_DELETE && !oap->is_visual && c > 0 && c + n >= ml_len(l) && !restart_edit)
				W->col = (size_t)head(ml_get(l), c - 1);
			/* del_bytes() refuses a count below 1 (E292): ]] with an
			   operator past a last line that starts with "}" ends before
			   the start (findpar(), then check_pos()) */
			if (n > 0) replace_bytes(l, c, n, "", 0);
			if (oap->op_type == OP_DELETE) check_cursor_col();
		} else {
			u_save_cursor();
			long sl = oap->start.lnum;
			size_t a = core_line_start(E, (size_t)sl) + (size_t)oap->start.col;
			size_t z = core_line_start(E, (size_t)oap->end.lnum) + (size_t)line_end_of(oap);
			/* op_delete() ends with do_join(2): the last line's marks go to
			   the start line, after its first start.col bytes */
			core_mark_join(E, (size_t)oap->end.lnum, 0, sl - oap->end.lnum, oap->start.col, 0);
			core_edit(E, a, z - a, "", 0);
			replace_bytes(sl, 0, 0, "", 0);   /* new generation for the line cache */
			if (oap->op_type == OP_DELETE) check_cursor_col();
		}
	}
setmarks:
	if (oap->motion_type == MBLOCK) { b_op_end.lnum = oap->end.lnum; b_op_end.col = oap->start.col; }
	else b_op_end = oap->start;
	b_op_start = oap->start;
	return 1;
}

/* ---- change ---- */

static struct { int active; Oparg oa; long pre_textlen, textcol, pre_indent; } change_block;

int op_change(Oparg *oap)
{
	long l = oap->start.col;
	if (oap->motion_type == MLINE) l = 0;
	if (is_empty_buf()) u_save_cursor();
	else if (!op_delete(oap)) return 0;
	if (l > (long)W->col && !lineempty((long)W->line) && !virtual_op) inc_cursor();
	change_block.active = 0;
	if (oap->motion_type == MBLOCK) {
		/* op_change(): spaces up to a block that starts past the line's end */
		if (virtual_op && (cur().coladd > 0 || gchar_cursor() == 0)) coladvance_force(getviscol());
		change_block.active = 1;
		change_block.oa = *oap;
		const char *s = ml_get(oap->start.lnum);
		change_block.pre_textlen = ml_len(oap->start.lnum);
		long i = 0;
		while (s[i] == ' ' || s[i] == '\t') i++;
		change_block.pre_indent = i;
		change_block.textcol = (long)W->col;
	}
	edit_start(0, 1);
	return 1;
}

/* the rest of op_change() once Insert mode ends: copy the text typed in the
   block's first line to the others */
void change_done(void)
{
	if (!change_block.active) return;
	change_block.active = 0;
	Oparg *oap = &change_block.oa;
	if (oap->start.lnum == oap->end.lnum) return;
	const char *s = ml_get(oap->start.lnum);
	long i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	long textcol = change_block.textcol, pre_textlen = change_block.pre_textlen;
	if (textcol > change_block.pre_indent) {
		pre_textlen += i - change_block.pre_indent;
		textcol += i - change_block.pre_indent;
	}
	long ins_len = ml_len(oap->start.lnum) - pre_textlen;
	if (ins_len <= 0) return;
	char *ins = malloc((size_t)ins_len);
	if (!ins) return;
	memcpy(ins, ml_get(oap->start.lnum) + textcol, (size_t)ins_len);
	/* a Del typed in the block's first line may have joined the lines
	   below: Neovim's ml_replace() fails past the last line */
	for (long ln = oap->start.lnum + 1; ln <= oap->end.lnum && ln < nlines(); ln++) {
		Block bd = { 0 };
		bprep(oap, &bd, ln, 1);
		if (!bd.is_short || virtual_op) {
			long pad = 0;
			if (bd.is_short) {
				long v = vcol_of(ln, ml_len(ln));
				pad = oap->start_vcol > v ? oap->start_vcol - v : 0;
			}
			Sbuf b = { 0 };
			bspaces(&b, pad);
			bput(&b, ins, ins_len);
			replace_bytes(ln, bd.textcol, 0, b.s, b.n);
			free(b.s);
		}
	}
	free(ins);
	check_cursor();
	/* text that went in before the cursor (a line break typed in the
	   block's first line puts the cursor on a copied line) can leave it
	   inside a character: Neovim's cursor ends on its first byte (measured,
	   "C-v j e y gg C-v j C x C-r \" Esc") */
	W->col = (size_t)head(ml_get((long)W->line), (long)W->col);
}

/* ---- shift ---- */

static void shift_block(Oparg *oap, long amount)
{
	int left = oap->op_type == OP_LSHIFT;
	long oldcol = (long)W->col, l = (long)W->line;
	Block bd = { 0 };
	bprep(oap, &bd, l, 1);
	if (bd.is_short) return;
	long total = amount * SW;
	const char *oldp = ml_get(l);
	long oldlen = ml_len(l);
	if (!left) {
		total += bd.pre_whitesp;
		long ts = bd.textcol;   /* textstart */
		if (bd.startspaces) {
			if (clen(oldp, oldlen, ts) == 1) ts++;
			else bd.startspaces = 0;
		}
		while (oldp[ts] == ' ' || oldp[ts] == '\t') {
			total += vcol_of(l, ts + 1) - vcol_of(l, ts);
			ts++;
		}
		long col_pre = bd.pre_whitesp_c - (bd.startspaces != 0);
		long tc = bd.textcol - col_pre;
		char *sp = malloc((size_t)total + 1);
		if (!sp) return;
		memset(sp, ' ', (size_t)total);
		replace_bytes(l, tc, ts - tc, sp, total);
		free(sp);
	} else {
		long nw = bd.textcol;
		if (bd.startspaces) nw += clen(oldp, oldlen, nw);
		long non_white_col = bd.start_vcol;
		while (oldp[nw] == ' ' || oldp[nw] == '\t') {
			non_white_col += vcol_of(l, nw + 1) - vcol_of(l, nw);
			nw++;
		}
		long block_space_width = non_white_col - oap->start_vcol;
		long shift_amount = block_space_width < total ? block_space_width : total;
		long destination_col = non_white_col - shift_amount;
		long vce = bd.textcol, vcw = bd.start_vcol;
		if (bd.startspaces) vcw -= bd.start_char_vcols;
		while (vcw < destination_col) {
			long incr = vcol_of(l, vce + clen(oldp, oldlen, vce)) - vcol_of(l, vce);
			if (vcw + incr > destination_col) break;
			vcw += incr;
			vce += clen(oldp, oldlen, vce);
		}
		long fill = destination_col - vcw;
		char sp[16];
		memset(sp, ' ', sizeof sp);
		replace_bytes(l, vce, nw - vce, sp, fill < 16 ? fill : 16);
	}
	W->col = (size_t)oldcol;
}

void op_shift(Oparg *oap, long amount)
{
	long block_col = 0;
	u_save_cursor();
	if (oap->motion_type == MBLOCK) block_col = (long)W->col;
	for (long i = oap->line_count - 1; i >= 0; i--) {
		long l = (long)W->line;
		if (ml_len(l) == 0) W->col = 0;
		else if (oap->motion_type == MBLOCK) shift_block(oap, amount);
		else {
			/* shift_line() without 'shiftround' */
			long count = get_indent(l);
			if (oap->op_type == OP_LSHIFT) { count -= SW * amount; if (count < 0) count = 0; }
			else count += SW * amount;
			set_indent(l, count);
		}
		W->line++;
	}
	if (oap->motion_type == MBLOCK) {
		W->line = (size_t)oap->start.lnum;
		W->col = (size_t)block_col;
	} else {
		W->line = (size_t)oap->start.lnum;
		beginline(BL_SOL | BL_FIX);
	}
	b_op_start = oap->start;
	b_op_end.lnum = oap->end.lnum;
	b_op_end.col = ml_len(oap->end.lnum);
	if (b_op_end.col > 0) b_op_end.col--;
}

/* ---- case ---- */

static const struct { unsigned first, last; int stride, delta; } upper_tab[] = {
#include "upper.inc"
}, lower_tab[] = {
#include "lower.inc"
};

int map_case(int c, int up)
{
	if (c < 0x80) {
		if (up && c >= 'a' && c <= 'z') return c - 32;
		if (!up && c >= 'A' && c <= 'Z') return c + 32;
		return c;
	}
	int n = up ? (int)(sizeof upper_tab / sizeof *upper_tab) : (int)(sizeof lower_tab / sizeof *lower_tab);
	for (int i = 0; i < n; i++) {
		unsigned f = up ? upper_tab[i].first : lower_tab[i].first, l = up ? upper_tab[i].last : lower_tab[i].last;
		int s = up ? upper_tab[i].stride : lower_tab[i].stride, d = up ? upper_tab[i].delta : lower_tab[i].delta;
		if ((unsigned)c < f) break;
		if ((unsigned)c <= l && ((unsigned)c - f) % (unsigned)s == 0) return c + d;
	}
	return c;
}

int swapchar(int op_type, Pos *pos)
{
	int c = gchar(*pos), nc = c;
	int up = map_case(c, 1), lo = map_case(c, 0);
	if (up != c) { if (op_type != OP_LOWER) nc = up; }        /* mb_islower() */
	else if (lo != c) { if (op_type != OP_UPPER) nc = lo; }   /* mb_isupper() */
	if (nc == c) return 0;
	char b[4];
	long n = uni_put((unsigned char *)b, (unsigned)nc);
	/* the first code point only: its combining marks stay (swapchar()
	   puts utf_char2bytes() over utf_ptr2len() bytes) */
	const unsigned char *s = (const unsigned char *)ml_get(pos->lnum);
	replace_bytes(pos->lnum, pos->col, (long)uni_len(s, (size_t)ml_len(pos->lnum), (size_t)pos->col), b, n);
	return 1;
}

static int swapchars(int op_type, Pos *pos, long length)
{
	int did = 0;
	for (long todo = length; todo > 0; todo--) {
		long n = ml_len(pos->lnum);
		if (pos->col < n) todo -= clen(ml_get(pos->lnum), n, pos->col) - 1;
		did |= swapchar(op_type, pos);
		if (inc(pos) == -1) break;
	}
	return did;
}

void op_tilde(Oparg *oap)
{
	u_save_cursor();
	Pos pos = oap->start;
	if (oap->motion_type == MBLOCK) {
		for (; pos.lnum <= oap->end.lnum; pos.lnum++) {
			Block bd = { 0 };
			bprep(oap, &bd, pos.lnum, 0);
			pos.col = bd.textcol;
			swapchars(oap->op_type, &pos, bd.textlen);
		}
	} else {
		if (oap->motion_type == MLINE) {
			oap->start.col = 0;
			pos.col = 0;
			oap->end.col = ml_len(oap->end.lnum);
			if (oap->end.col) oap->end.col--;
		} else if (!oap->inclusive) dec(&oap->end);
		if (pos.lnum == oap->end.lnum) swapchars(oap->op_type, &pos, oap->end.col - pos.col + 1);
		else
			for (;;) {
				swapchars(oap->op_type, &pos, pos.lnum == oap->end.lnum ? oap->end.col + 1 : ml_len(pos.lnum) - pos.col);
				if (!lt(pos, oap->end) || inc(&pos) == -1) break;
			}
	}
	b_op_start = oap->start;
	b_op_end = oap->end;
}

/* ---- replace (Visual r) ---- */

int op_replace(Oparg *oap, int c)
{
	if (is_empty_buf() || oap->empty) return 1;
	u_save_cursor();
	char cb[4];
	long cn = uni_put((unsigned char *)cb, (unsigned)c);
	if (oap->motion_type == MBLOCK) {
		int is_MAX = W->want == WANT_EOL;
		for (; (long)W->line <= oap->end.lnum; W->line++) {
			long l = (long)W->line;
			W->col = 0;
			Block bd = { 0 };
			bprep(oap, &bd, l, 1);
			if (bd.textlen == 0 && (!virtual_op || is_MAX)) continue;
			if (virtual_op && bd.is_short && bd.textcol >= ml_len(l)) {
				long v = vcol_of(l, ml_len(l));
				bd.startspaces += oap->start_vcol > v ? oap->start_vcol - v : 0;
			}
			long numc = oap->end_vcol - oap->start_vcol + 1;
			if (bd.is_short && (!virtual_op || is_MAX)) numc -= (oap->end_vcol - bd.end_vcol) + 1;
			/* a double-wide character fills two columns of the block */
			if (uni_width((const unsigned char *)cb, (size_t)cn, 0) > 1) {
				if (numc % 2 && !bd.is_short) bd.endspaces++;
				numc /= 2;
			}
			Sbuf b = { 0 };
			bspaces(&b, bd.startspaces);
			if (c != '\r' && c != '\n') {
				for (long i = 0; i < numc; i++) bput(&b, cb, cn);
				if (!bd.is_short) bspaces(&b, bd.endspaces);
				long del = bd.is_short ? ml_len(l) - bd.textcol : bd.textlen;
				replace_bytes(l, bd.textcol, del, b.s ? b.s : "", b.n);
			} else {
				/* the rest of the line goes to a new line */
				bput(&b, "\n", 1);
				replace_bytes(l, bd.textcol, bd.textlen, b.s, b.n);
				W->line++;
				oap->end.lnum++;
			}
			free(b.s);
		}
	} else {
		if (oap->motion_type == MLINE) {
			oap->start.col = 0;
			W->col = 0;
			oap->end.col = ml_len(oap->end.lnum);
			if (oap->end.col) oap->end.col = head(ml_get(oap->end.lnum), oap->end.col - 1);
		} else if (!oap->inclusive) dec(&oap->end);
		while (!lt(oap->end, cur())) {
			Pos p = cur();
			long len = ml_len(p.lnum);
			if (p.col < len) {
				long ol = clen(ml_get(p.lnum), len, p.col);
				if (p.lnum == oap->end.lnum) oap->end.col += cn - ol;
				if (c == '\r' || c == '\n') {
					/* a line break replaces the character (only via CTRL-V) */
					replace_bytes(p.lnum, p.col, ol, cb, cn);
				} else replace_bytes(p.lnum, p.col, ol, cb, cn);
			}
			if (inc_cursor() == -1) break;
		}
	}
	setcur(oap->start);
	check_cursor();
	b_op_start = oap->start;
	b_op_end = oap->end;
	return 1;
}

/* ---- block insert and append (Visual block I and A) ---- */

static struct { int active; Oparg oa; Block bd; long pre_textlen, ind_pre_col, ind_pre_vcol; Pos start_insert; } ins_block;

void op_insert(Oparg *oap, long count1)
{
	Block bd = { 0 };
	memset(&bd, 0, sizeof bd);
	bd.is_MAX = W->want == WANT_EOL;
	W->line = (size_t)oap->start.lnum;
	ins_block.active = 0;
	if (oap->motion_type == MBLOCK) {
		/* 'virtualedit': the spaces go in before block_prep() looks */
		if (cur().coladd > 0) {
			u_save_cursor();
			coladvance_force(oap->op_type == OP_APPEND ? oap->end_vcol + 1 : getviscol());
			if (oap->op_type == OP_APPEND) W->col--;
		}
		bprep(oap, &bd, oap->start.lnum, 1);
		const char *s = ml_get((long)W->line);
		long i = 0;
		while (s[i] == ' ' || s[i] == '\t') i++;
		ins_block.ind_pre_col = i;
		ins_block.ind_pre_vcol = get_indent((long)W->line);
		ins_block.pre_textlen = ml_len(oap->start.lnum) - bd.textcol;
		if (oap->op_type == OP_APPEND) ins_block.pre_textlen -= bd.textlen;
	}
	if (oap->op_type == OP_APPEND) {
		if (oap->motion_type == MBLOCK && cur().coladd == 0) {
			set_curswant = 1;
			while (gchar_cursor() != 0 && (long)W->col < bd.textcol + bd.textlen) W->col++;
			if (bd.is_short && !bd.is_MAX) {
				u_save_cursor();
				char sp[256];
				memset(sp, ' ', sizeof sp);
				long k = bd.endspaces < 256 ? bd.endspaces : 256;
				replace_bytes((long)W->line, (long)W->col, 0, sp, k);
				W->col += (size_t)k;
				bd.textlen += bd.endspaces;
			}
		} else {
			setcur(oap->end);
			check_cursor_col();
			/* Neovim can stay inside a character here and then writes a
			   split one (src/keys/README, not copied): keep to its head */
			W->col = (size_t)head(ml_get((long)W->line), (long)W->col);
			if (!lineempty((long)W->line) && oap->start_vcol != oap->end_vcol) inc_cursor();
		}
	}
	if (oap->motion_type == MBLOCK) {
		ins_block.active = 1;
		ins_block.oa = *oap;
		ins_block.bd = bd;
		ins_block.start_insert = cur();
	}
	edit_start(0, count1);
}

static void block_insert(Oparg *oap, const char *s, long slen, int b_insert, Block *bdp)
{
	/* a Del in the insert can join the block's lines away; Neovim's
	   ml_get() past the last line changes nothing */
	for (long l = oap->start.lnum + 1; l <= oap->end.lnum && l < nlines(); l++) {
		bprep(oap, bdp, l, 1);
		if (bdp->is_short && b_insert) continue;
		const char *oldp = ml_get(l);
		long ts_val, spaces = 0, count = 0, offset;
		if (b_insert) {
			ts_val = bdp->start_char_vcols;
			spaces = bdp->startspaces;
			if (spaces != 0) count = ts_val - 1;
			offset = bdp->textcol;
		} else {
			ts_val = bdp->end_char_vcols;
			if (!bdp->is_short) {
				spaces = bdp->endspaces ? ts_val - bdp->endspaces : 0;
				if (spaces != 0) count = ts_val - 1;
				offset = bdp->textcol + bdp->textlen - (spaces != 0);
			} else {
				if (!bdp->is_MAX) spaces = oap->end_vcol - bdp->end_vcol + 1;
				count = spaces;
				offset = bdp->textcol + bdp->textlen;
			}
		}
		if (spaces > 0) offset = head(oldp, offset);
		if (spaces < 0) spaces = 0;
		Sbuf b = { 0 };
		bspaces(&b, spaces);
		bput(&b, s, slen);
		long del = 0;
		if (spaces > 0 && !bdp->is_short) {
			if (oldp[offset] == '\t') {
				bspaces(&b, ts_val - spaces);
				del = 1;
				count++;
			} else count = spaces;
		}
		/* the padding count replaces "spaces" when a tab was cut */
		if (spaces > 0 && b.n < spaces + slen + count - spaces) { /* nothing: count spaces already in */ }
		replace_bytes(l, offset, del, b.s ? b.s : "", b.n);
		free(b.s);
		if (l == oap->end.lnum) { b_op_end.lnum = oap->end.lnum; b_op_end.col = offset; }
	}
}

/* the rest of op_insert() once Insert mode ends */
void insert_block_done(Pos insstart)
{
	if (!ins_block.active) return;
	ins_block.active = 0;
	Oparg *oap = &ins_block.oa;
	Block bd = ins_block.bd, bd2 = { 0 };
	if ((long)W->line != oap->start.lnum) return;
	const char *s = ml_get(oap->start.lnum);
	long i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	long ind_post_col = i, ind_post_vcol = 0;
	int did_indent = 0;
	long pre_textlen = ins_block.pre_textlen;
	if (b_op_start.col > ins_block.ind_pre_col && ind_post_col > ins_block.ind_pre_col) {
		bd.textcol += ind_post_col - ins_block.ind_pre_col;
		ind_post_vcol = get_indent(oap->start.lnum);
		bd.start_vcol += ind_post_vcol - ins_block.ind_pre_vcol;
		did_indent = 1;
	}
	/* the cursor moved before anything was typed (C-v I Right x): the
	   block starts where the insert did */
	if (oap->start.lnum == b_op_start_orig.lnum && !bd.is_MAX && !did_indent) {
		long t = vcol_of(b_op_start_orig.lnum, b_op_start_orig.col) + b_op_start_orig.coladd;
		if (oap->op_type == OP_INSERT && oap->start.col + oap->start.coladd != b_op_start_orig.col + b_op_start_orig.coladd) {
			oap->start.col = b_op_start_orig.col;
			pre_textlen -= t - oap->start_vcol;
			oap->start_vcol = t;
		} else if (oap->op_type == OP_APPEND && oap->start.col + oap->start.coladd >= b_op_start_orig.col + b_op_start_orig.coladd) {
			oap->start.col = b_op_start_orig.col;
			pre_textlen += bd.textlen;
			pre_textlen -= t - oap->start_vcol;
			oap->start_vcol = t;
			oap->op_type = OP_INSERT;
		}
	}
	if (did_indent && bd.textcol - ind_post_col > 0) {
		oap->start.col += ind_post_col - ins_block.ind_pre_col;
		oap->start_vcol += ind_post_vcol - ins_block.ind_pre_vcol;
		oap->end.col += ind_post_col - ins_block.ind_pre_col;
		oap->end_vcol += ind_post_vcol - ins_block.ind_pre_vcol;
	}
	bprep(oap, &bd2, oap->start.lnum, 1);
	if (did_indent && bd.textcol - ind_post_col > 0) {
		oap->start.col -= ind_post_col - ins_block.ind_pre_col;
		oap->start_vcol -= ind_post_vcol - ins_block.ind_pre_vcol;
		oap->end.col -= ind_post_col - ins_block.ind_pre_col;
		oap->end_vcol -= ind_post_vcol - ins_block.ind_pre_vcol;
	}
	if (!bd.is_MAX || bd2.textlen < bd.textlen) {
		if (oap->op_type == OP_APPEND) {
			pre_textlen += bd2.textlen - bd.textlen;
			if (bd2.endspaces) bd2.textlen--;
		}
		bd.textcol = bd2.textcol;
		bd.textlen = bd2.textlen;
	}
	long len = ml_len(oap->start.lnum), add = bd.textcol, offset = 0;
	if (oap->op_type == OP_APPEND) {
		add += bd.textlen;
		Pos st = ins_block.start_insert;
		if (bd.is_MAX && st.lnum == insstart.lnum && st.col > insstart.col) {
			offset = st.col - insstart.col;
			add -= offset;
			if (oap->end_vcol > offset) oap->end_vcol -= offset + 1;
			else return;
		}
	}
	if (add > len) add = len;
	long ins_len = len - add - pre_textlen - offset;
	if (pre_textlen >= 0 && ins_len > 0) {
		char *t = malloc((size_t)ins_len);
		if (!t) return;
		memcpy(t, ml_get(oap->start.lnum) + add, (size_t)ins_len);
		block_insert(oap, t, ins_len, oap->op_type == OP_INSERT, &bd);
		W->col = (size_t)oap->start.col;
		check_cursor();
		free(t);
	}
}

/* ---- join ---- */

int do_join(long count, int insert_space)
{
	long l0 = (long)W->line;
	u_save_cursor();
	Sbuf b = { 0 };
	long col = 0, currsize = 0;
	int endcurr1 = 0, endcurr2 = 0;
	bput(&b, "", 0);
	for (long t = 0; t < count; t++) {
		const char *curr = ml_get(l0 + t);
		if (t == 0) { b_op_start.lnum = l0; b_op_start.col = ml_len(l0); }
		long spaces = 0;
		if (insert_space && t > 0) {
			while (*curr == ' ' || *curr == '\t') curr++;
			if (*curr != 0 && *curr != ')' && b.n != 0 && endcurr1 != '\t') {
				/* 'formatoptions' j with 'comments' at their default would
				   remove a comment leader here; see FEATURES.md */
				if (endcurr1 == ' ') endcurr1 = endcurr2;
				else spaces++;
			}
		}
		currsize = (long)strlen(curr);
		col = b.n;      /* the cursor goes on the space inserted */
		/* mark_col_adjust() for each joined line, before the edit; the
		   spaces skipped beyond those inserted are spaces_removed */
		long removed = (long)(curr - ml_get(l0 + t)) - spaces;
		core_mark_join(E, (size_t)(l0 + t), 0, -t, col - removed, (int)removed);
		bspaces(&b, spaces);
		bput(&b, curr, currsize);
		endcurr1 = endcurr2 = 0;
		if (insert_space && currsize > 0) {
			long e = head(curr, currsize - 1);
			Pos q = { l0 + t, (long)(curr - ml_get(l0 + t)) + e, 0 };
			endcurr1 = gchar(q);
			if (e > 0) {
				q.col = (long)(curr - ml_get(l0 + t)) + head(curr, e - 1);
				endcurr2 = gchar(q);
			}
		}
	}
	/* one edit over the joined lines; core_mark_join() marks the step a
	   join, so lines that join to nothing undo and redo as do_join()'s */
	size_t a = core_line_start(E, (size_t)l0);
	size_t z = core_line_start(E, (size_t)(l0 + count)) - 1;
	long len = b.n;
	core_edit(E, a, z - a, b.s, (size_t)b.n);
	replace_bytes(l0, 0, 0, "", 0);
	free(b.s);
	b_op_end.lnum = l0;
	b_op_end.col = len;
	W->line = (size_t)l0;
	W->col = (size_t)col;
	check_cursor_col();
	set_curswant = 1;
	return 1;
}

/* ---- put ---- */

void do_put(int regname, int dir, long count, int flags)
{
	long lnum;
	int y_type;
	long y_width = 0;
	Reg tmp = { 0 }, *reg;
	char *alloc = NULL;
	b_op_start = cur();
	b_op_end = cur();
	if (regname == '.') {
		/* ". is put by inserting it again; Visual mode and the cursor at
		   the end are not here yet */
		if (VIsual_active || (flags & (PUT_LINE | PUT_CURSEND))) { beep(); return; }
		stuff_inserted(dir == FORWARD ? 'a' : 'i', count, 0);
		if (dir == FORWARD) u_save_cursor();
		return;
	}
	if (regname == ':' || regname == '%' || regname == '/' || regname == '_') {
		/* get_spec_reg(): the black hole puts as an empty string */
		const char *s = NULL;
		long n = 0;
		if (regname == '_') s = "";
		else if (regname == ':') {
			if (!last_cmdline) { core_msg(E, "E30: No previous command line"); return; }
			s = last_cmdline;
			n = (long)strlen(s);
		} else if (regname == '%') {
			s = B->path ? B->path : "";
			n = (long)strlen(s);
		} else return;
		alloc = malloc((size_t)n + 1);
		if (!alloc) return;
		memcpy(alloc, s, (size_t)n);
		tmp.s = alloc;
		tmp.n = n;
		tmp.type = MCHAR;
		reg = &tmp;
	} else reg = put_override ? put_override : get_reg(regname, 0);
	/* do_put(): linewise, u_save(lnum - 1, lnum) at the line put before */
	if (reg->type == MLINE && !(flags & PUT_LINE_SPLIT) && !is_empty_buf()) u_save(W->line + (dir == FORWARD), W->line + (dir == FORWARD));
	else u_save_cursor();
	y_type = reg->type;
	y_width = reg->width;
	long y_size = reg_lines(reg);
	if (y_type == MLINE) {
		if (flags & PUT_LINE_SPLIT) {
			/* "p" in Visual char mode with a linewise register: the line is
			   split at the cursor */
			long l = (long)W->line, c = (long)W->col, n = ml_len(l);
			if (dir == FORWARD && c < n) c += clen(ml_get(l), n, c);
			replace_bytes(l, c, 0, "\n", 1);
			dir = FORWARD;
		}
		if (flags & PUT_LINE_FORWARD) {
			/* "p" in Visual block: the lines go below the block */
			setcur(visual_end());
			dir = FORWARD;
		}
		b_op_start = cur();
		b_op_end = cur();
	}
	if (flags & PUT_LINE) y_type = MLINE;
	if (y_size == 0 || !reg->s) {
		core_msg(E, "E353: Nothing in register %c", regname ? regname : '"');
		goto end;
	}
	if (y_type == MBLOCK) {
		long col;
		int c = gchar_cursor();
		if (dir == FORWARD && c != 0) {
			col = vcol_end((long)W->line, (long)W->col);
			W->col += (size_t)clen(ml_get((long)W->line), ml_len((long)W->line), (long)W->col);
			col++;
		} else col = vcol_of((long)W->line, (long)W->col);
		lnum = (long)W->line;
		long totlen = 0, textcol = 0;
		for (long i = 0; i < y_size; i++) {
			if ((long)W->line >= nlines()) append_lines(nlines() - 1, "", 0);
			long l = (long)W->line;
			const char *oldp = ml_get(l);
			long oldlen = ml_len(l), vcol = 0, j = 0, incr = 0;
			long startspaces = 0, endspaces = 0, delcount = 0;
			if (col > 0) {            /* do_put()'s walk to col */
				j = walk(l, oldp, oldlen, 0, col, &(long){ 0 }, &incr);
				vcol = vcol_of(l, j);
			}
			textcol = j;
			int shortline = vcol < col || (vcol == col && j >= oldlen);
			if (vcol < col) startspaces = col - vcol;
			else if (vcol > col) {
				endspaces = vcol - col;
				startspaces = incr - endspaces;
				textcol = head(oldp, textcol - 1);
				delcount = 1;
				if (oldp[textcol] != '\t') { delcount = 0; endspaces = 0; }
			}
			long yl;
			const char *ys = reg_line(reg, i, &yl);
			long spaces = y_width + 1;
			/* win_charsize() at column 0: a Tab is 'tabstop' (8), ^X two */
			for (long q = 0; q < yl; q += clen(ys, yl, q)) {
				unsigned char c = (unsigned char)ys[q];
				spaces -= c == '\t' ? 8 : c < 0x20 || c == 0x7f ? 2 : uni_width((const unsigned char *)ys, (size_t)yl, (size_t)q);
			}
			if (spaces < 0) spaces = 0;
			Sbuf b = { 0 };
			bspaces(&b, startspaces);
			for (long r = 0; r < count; r++) {
				bput(&b, ys, yl);
				if ((r < count - 1 || !shortline) && spaces > 0) bspaces(&b, spaces);
			}
			bspaces(&b, endspaces);
			totlen = b.n;   /* with the start and end spaces, as do_put() counts it */
			replace_bytes(l, textcol, delcount, b.s ? b.s : "", b.n);
			free(b.s);
			W->line++;
			if (i == 0) W->col += (size_t)startspaces;
		}
		b_op_start = cur();
		b_op_start.lnum = lnum;
		b_op_end.lnum = (long)W->line - 1;
		b_op_end.col = textcol + totlen - 1 > 0 ? textcol + totlen - 1 : 0;
		if (flags & PUT_CURSEND) {
			setcur(b_op_end);
			W->col++;
			long len = ml_len((long)W->line);
			if ((long)W->col > len) W->col = (size_t)len;
		} else W->line = (size_t)lnum;
	} else {
		lnum = (long)W->line;
		long col = (long)W->col;
		long yl0;
		const char *y0 = reg_line(reg, 0, &yl0);
		if (y_type == MCHAR) {
			if (dir == FORWARD && gchar_cursor() != 0) {
				long bl = clen(ml_get(lnum), ml_len(lnum), col);
				col += bl;
				if (yl0) { W->col += (size_t)bl; b_op_end.col += bl; }
			}
			b_op_start = cur();
		} else if (dir == BACKWARD) lnum--;
		Pos new_cursor = cur();
		if (y_type == MCHAR && y_size == 1) {
			/* in Visual mode the text goes in every line of the selection,
			   at the same screen column */
			long totlen = count * yl0, end_lnum = 0, start_lnum = lnum, vcol = 0, fbo = 0;
			if (VIsual_active) {
				/* curbuf->b_visual's end, as mark_adjust() left it: deleting a
				   charwise selection joins its lines, which moves the end onto
				   the first; a block deletes no line */
				end_lnum = VIsual_mode == 22 ? visual_last_line() : lnum;
				if (end_lnum > start_lnum) vcol = vcol_of(lnum, col);
			}
			if (totlen == 0) {
				if (VIsual_active) lnum = end_lnum;
			} else {
				Sbuf b = { 0 };
				for (long r = 0; r < count; r++) bput(&b, y0, yl0);
				do {
					long oldlen = ml_len(lnum);
					if (lnum > start_lnum) {
						/* getvpos(): the character at vcol, MAXCOL past the end
						   unless 'virtualedit' reaches it (a block: the end) */
						col = col_of_vcol(lnum, vcol, 1);
						if (vcol_of(lnum, col) < vcol && !virtual_active()) col = MAXCOL;
					}
					if (VIsual_active && col > oldlen) { lnum++; continue; }
					replace_bytes(lnum, col, 0, b.s, b.n);
					fbo = col + totlen - 1 - head(ml_get(lnum), col + totlen - 1);
					if (lnum == (long)W->line) W->col += (size_t)(totlen - 1);
					if (VIsual_active) lnum++;
				} while (VIsual_active && lnum <= end_lnum);
				if (VIsual_active) lnum--;
				free(b.s);
			}
			b_op_end = cur();
			b_op_end.col -= fbo;
			if (totlen && (restart_edit || (flags & PUT_CURSEND))) W->col++;
			else W->col -= (size_t)fbo;
		} else {
			long new_lnum = new_cursor.lnum;
			for (long cnt = 1; cnt <= count; cnt++) {
				long i = 0;
				if (y_type == MCHAR) {
					/* split the line: the first register line ends it, the
					   last one starts the rest */
					lnum = new_cursor.lnum;
					Sbuf b = { 0 };
					bput(&b, y0, yl0);
					for (long k = 1; k < y_size; k++) {
						long yl;
						const char *ys = reg_line(reg, k, &yl);
						bput(&b, "\n", 1);
						bput(&b, ys, yl);
					}
					replace_bytes(lnum, col, 0, b.s, b.n);
					free(b.s);
					W->line = (size_t)lnum;
					new_lnum += y_size - 1;
					lnum += y_size - 1;
					i = y_size;
				} else {
					Sbuf b = { 0 };
					for (; i < y_size; i++) {
						long yl;
						const char *ys = reg_line(reg, i, &yl);
						if (i) bput(&b, "\n", 1);
						bput(&b, ys, yl);
					}
					append_lines(lnum, b.s ? b.s : "", b.n);
					free(b.s);
					lnum += y_size;
					new_lnum += y_size;
					if (flags & PUT_FIXINDENT) { /* ]p: later, with the indent package */ }
				}
				if (cnt == 1) new_lnum = lnum;
			}
			if (y_type == MLINE) {
				b_op_start.col = 0;
				if (dir == FORWARD) b_op_start.lnum++;
			}
			b_op_end.lnum = new_lnum;
			long yll;
			/* do_put(): '] is the start of the last character, col - 1 minus
			   utf_head_off(); the register line has no NUL, so not head() */
			const char *yle = reg_line(reg, y_size - 1, &yll);
			b_op_end.col = yll > 1 ? (long)uni_gprev((const unsigned char *)yle, (size_t)yll, (size_t)yll) : 0;
			if (flags & PUT_CURSEND) {
				if (y_type == MLINE) {
					W->line = (size_t)(lnum + 1 >= nlines() ? nlines() - 1 : lnum + 1);
					W->col = 0;
				} else {
					W->line = (size_t)new_lnum;
					W->col = (size_t)yll;
				}
			} else if (y_type == MLINE) {
				W->col = 0;
				if (dir == FORWARD) W->line++;
				beginline(BL_WHITE | BL_FIX);
			} else setcur(new_cursor);
		}
	}
	set_curswant = 1;
	if ((long)W->col > ml_len((long)W->line)) W->col = (size_t)ml_len((long)W->line);
end:
	VIsual_active = 0;
	adjust_cursor_eol();
	free(alloc);
}

/* ---- CTRL-A and CTRL-X ---- */

static int isxd(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int isbd(int c) { return c == '0' || c == '1'; }
static int isdg(int c) { return c >= '0' && c <= '9'; }

/* vim_str2nr() for 'nrformats' bin,hex */
static void str2nr(const char *s, int *prep, long *len, unsigned long long *nptr, long maxlen, int *overflow)
{
	const char *p = s;
	int pre = 0;
	unsigned long long un = 0;
	*overflow = 0;
	if (*p == '-') p++;
	if (p[0] == '0' && p[1] != '8' && p[1] != '9' && (maxlen == 0 || maxlen > 1 + (p - s))) {
		pre = (unsigned char)p[1];
		if ((pre == 'X' || pre == 'x') && isxd((unsigned char)p[2]) && (maxlen == 0 || maxlen > 2 + (p - s))) p += 2;
		else if ((pre == 'B' || pre == 'b') && isbd((unsigned char)p[2]) && (maxlen == 0 || maxlen > 2 + (p - s))) p += 2;
		else pre = 0;
	}
	long n = (long)(p - s);
#define LIM (maxlen == 0 || n < maxlen)
	if (pre == 'B' || pre == 'b') {
		while (isbd((unsigned char)*p) && LIM) {
			if (un <= ~0ULL / 2) un = 2 * un + (unsigned long long)(*p - '0');
			else { un = ~0ULL; *overflow = 1; }
			p++; n++;
		}
	} else if (pre == 'X' || pre == 'x') {
		while (isxd((unsigned char)*p) && LIM) {
			int d = *p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10;
			if (un <= ~0ULL / 16) un = 16 * un + (unsigned long long)d;
			else { un = ~0ULL; *overflow = 1; }
			p++; n++;
		}
	} else {
		while (isdg((unsigned char)*p) && LIM) {
			unsigned long long d = (unsigned long long)(*p - '0');
			if (un < ~0ULL / 10 || (un == ~0ULL / 10 && d <= ~0ULL % 10)) un = 10 * un + d;
			else { un = ~0ULL; *overflow = 1; }
			p++; n++;
		}
	}
#undef LIM
	*prep = pre;
	*len = n;
	*nptr = un;
}

static int do_addsub(int op_type, Pos *pos, long length, long Prenum1, int visual)
{
	static int hexupper;
	int pre, negative = 0, was_positive = 1, did_change = 0;
	Pos save_cursor = cur();
	long maxlen = 0;
	setcur(*pos);
	const char *ptr = ml_get(pos->lnum);
	long linelen = ml_len(pos->lnum), col = pos->col;
	unsigned long long n;
	if (col >= linelen) goto theend;
	if (!visual) {
		while (col > 0 && isbd((unsigned char)ptr[col])) { col--; col = head(ptr, col); }
		while (col > 0 && isxd((unsigned char)ptr[col])) { col--; col = head(ptr, col); }
		if (!(col > 0 && (ptr[col] == 'X' || ptr[col] == 'x') && ptr[col - 1] == '0' && isxd((unsigned char)ptr[col + 1]))) {
			col = (long)W->col;
			while (col > 0 && isdg((unsigned char)ptr[col])) { col--; col = head(ptr, col); }
		}
		if ((col > 0 && (ptr[col] == 'X' || ptr[col] == 'x') && ptr[col - 1] == '0' && isxd((unsigned char)ptr[col + 1]))
		    || (col > 0 && (ptr[col] == 'B' || ptr[col] == 'b') && ptr[col - 1] == '0' && isbd((unsigned char)ptr[col + 1]))) {
			col--;
			col = head(ptr, col);
		} else {
			col = pos->col;
			while (ptr[col] != 0 && !isdg((unsigned char)ptr[col])) col++;
			while (col > 0 && isdg((unsigned char)ptr[col - 1])) col--;
		}
	}
	if (visual) {
		while (ptr[col] != 0 && length > 0 && !isdg((unsigned char)ptr[col])) {
			int k = clen(ptr, linelen, col);
			col += k;
			length -= k;
		}
		if (length == 0) goto theend;
		if (col > pos->col && ptr[col - 1] == '-') { negative = 1; was_positive = 0; }
	}
	int firstdigit = (unsigned char)ptr[col];
	if (!isdg(firstdigit)) { beep(); goto theend; }
	if (col > 0 && ptr[col - 1] == '-' && !visual) { col--; negative = 1; }
	if (visual && VIsual_mode != 'V') maxlen = W->want == WANT_EOL ? linelen - col : length;
	int overflow;
	str2nr(ptr + col, &pre, &length, &n, maxlen, &overflow);
	if (pre && negative) { col++; length--; negative = 0; }
	int subtract = op_type == OP_NR_SUB;
	if (negative) subtract ^= 1;
	unsigned long long oldn = n;
	if (!overflow) n = subtract ? n - (unsigned long long)Prenum1 : n + (unsigned long long)Prenum1;
	if (!pre) {
		if (subtract) { if (n > oldn) { n = 1 + (n ^ ~0ULL); negative ^= 1; } }
		else if (n < oldn) { n = n ^ ~0ULL; negative ^= 1; }
		if (n == 0) negative = 0;
	}
	if (visual && !was_positive && !negative && col > 0) { col--; length++; }
	W->col = (size_t)col;
	Pos startpos = cur();
	did_change = 1;
	{
		/* the old number goes; a hex number keeps the case of its last letter */
		for (long i = 0; i < length; i++) {
			int c = (unsigned char)ptr[col + i];
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) hexupper = c >= 'A' && c <= 'Z';
		}
		if (ptr[col] == '-') length--;
	}
	/* buf1 as Neovim's: length + NUMBUFLEN, so every leading zero is kept */
	char *buf1 = malloc((size_t)length + 100), buf2[100];
	if (!buf1) return 0;
	long b1 = 0;
	if (negative && (!visual || was_positive)) buf1[b1++] = '-';
	if (pre) { buf1[b1++] = '0'; length--; }
	if (pre == 'b' || pre == 'B' || pre == 'x' || pre == 'X') { buf1[b1++] = (char)pre; length--; }
	int b2 = 0;
	if (pre == 'b' || pre == 'B') {
		int bits;
		for (bits = 64; bits > 0; bits--) if ((n >> (bits - 1)) & 1) break;
		while (bits > 0) buf2[b2++] = ((n >> --bits) & 1) ? '1' : '0';
		buf2[b2] = 0;
	} else if (pre == 0) b2 = snprintf(buf2, sizeof buf2, "%llu", n);
	else if (hexupper) b2 = snprintf(buf2, sizeof buf2, "%llX", n);
	else b2 = snprintf(buf2, sizeof buf2, "%llx", n);
	length -= b2;
	if (firstdigit == '0') while (length-- > 0) buf1[b1++] = '0';
	memcpy(buf1 + b1, buf2, (size_t)b2);
	b1 += b2;
	{
		/* the deleted span: the sign if any, then the parsed digits */
		int ov, p2;
		long todel;
		unsigned long long dummy;
		str2nr(ptr + col, &p2, &todel, &dummy, maxlen, &ov);
		replace_bytes(pos->lnum, col, todel, buf1, b1);
		free(buf1);
	}
	W->col = (size_t)(col + b1);
	Pos endpos = cur();
	if (W->col) W->col--;
	b_op_start = startpos;
	b_op_end = endpos;
	if (b_op_end.col > 0) b_op_end.col--;
theend:
	if (visual) setcur(save_cursor);
	else if (did_change) set_curswant = 1;
	return did_change;
}

void op_addsub(Oparg *oap, long Prenum1, int g_cmd)
{
	long amount = Prenum1;
	if (!VIsual_active) {
		Pos pos = cur();
		u_save_cursor();
		do_addsub(oap->op_type, &pos, 0, amount, 0);
		return;
	}
	u_save_cursor();
	Pos pos = oap->start;
	for (; pos.lnum <= oap->end.lnum; pos.lnum++) {
		long length;
		if (oap->motion_type == MBLOCK) {
			Block bd = { 0 };
			bprep(oap, &bd, pos.lnum, 0);
			pos.col = bd.textcol;
			length = bd.textlen;
		} else if (oap->motion_type == MLINE) {
			W->col = 0;
			pos.col = 0;
			length = ml_len(pos.lnum);
		} else {
			if (pos.lnum == oap->start.lnum && !oap->inclusive) dec(&oap->end);
			length = ml_len(pos.lnum);
			pos.col = 0;
			if (pos.lnum == oap->start.lnum) { pos.col += oap->start.col; length -= oap->start.col; }
			if (pos.lnum == oap->end.lnum) {
				length = ml_len(oap->end.lnum);
				if (oap->end.col > length - 1) oap->end.col = length - 1;
				length = oap->end.col - pos.col + 1;
			}
		}
		int one = do_addsub(oap->op_type, &pos, length, amount, 1);
		if (g_cmd && one) amount += Prenum1;
	}
}

/* ---- recording and executing registers ---- */

int reg_recording, reg_recorded;
static int execreg_lastc;

/* keys as register bytes: specials as Neovim's K_SPECIAL triples */
static const struct { int key; char a, b; } special[] = {
	{ KEY_UP, 'k', 'u' }, { KEY_DOWN, 'k', 'd' }, { KEY_LEFT, 'k', 'l' }, { KEY_RIGHT, 'k', 'r' }, { KEY_BS, 'k', 'b' }, { KEY_DEL, 'k', 'D' },
};

long key_to_bytes(int k, char *b)
{
	for (unsigned i = 0; i < sizeof special / sizeof *special; i++)
		if (special[i].key == k) { b[0] = (char)0x80; b[1] = special[i].a; b[2] = special[i].b; return 3; }
	if (k >= 0x110000) return 0;
	return uni_put((unsigned char *)b, (unsigned)k);
}

/* the keys a register's bytes stand for, to the stuff buffer (CTRL-R) or
   the typeahead (@) */
/* typeahead: into the typeahead (an executed register), else the stuff
   buffer; literally: stuffescaped() puts CTRL-V before a control
   character so Insert mode inserts it */
static void stuff_bytes(const char *s, long n, int typeahead, int literally)
{
	int *k = malloc(sizeof(int) * (size_t)(2 * n + 1));
	if (!k) return;
	long m = 0;
	for (long i = 0; i < n;) {
		/* stuffescaped(): literally, a recorded key's K_SPECIAL bytes go in
		   as text (0x80 escaped by stuffcharReadbuff()), not as the key, so
		   "qj i x BS Esc q" then "a C-r j" inserts x<80>kb<1b> as Neovim
		   does; an executed register (@) types them as keys */
		if ((unsigned char)s[i] == 0x80 && i + 2 < n && !literally) {
			int found = 0;
			for (unsigned j = 0; j < sizeof special / sizeof *special; j++)
				if (special[j].a == s[i + 1] && special[j].b == s[i + 2]) { k[m++] = special[j].key; found = 1; }
			if (found) { i += 3; continue; }
		}
		/* vgetorpeek() reads typeahead a character at a time (utf_ptr2len),
		   so a combining mark is a key of its own */
		int l = (int)uni_len((const unsigned char *)s, (size_t)n, (size_t)i);
		if (literally && l == 1 && (((unsigned char)s[i] < ' ' && s[i] != '\t') || s[i] == 0x7f)) k[m++] = 22;
		if (l == 1) k[m++] = (unsigned char)s[i];
		else k[m++] = (int)uni_cp((const unsigned char *)s, (size_t)n, (size_t)i);
		i += l;
	}
	if (typeahead) typebuf_ins(k, m);
	else stuff(k, m);
	free(k);
}

static Sbuf recordbuff;
static long recordbuff_last;

/* a fresh session's registers, for tests that must not depend on the
   inputs before them */
void ops_reset(void)
{
	for (int i = 0; i < 38; i++) { free(y_regs[i].s); y_regs[i] = (Reg){ 0 }; }
	y_previous = NULL;
	free(last_cmdline);
	last_cmdline = NULL;
	recordbuff.n = recordbuff_last = 0;
	reg_recording = reg_recorded = execreg_lastc = 0;
}

void record_key(int k)
{
	char b[4];
	long n = key_to_bytes(k, b);
	bput(&recordbuff, b, n);
	recordbuff_last = n;
}

int do_record(int c)
{
	static int regname;
	if (reg_recording == 0) {
		if (c < 0 || c >= 0x80 || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '"'))
			return 0;
		reg_recording = c;
		regname = c;
		recordbuff.n = 0;
		return 1;
	}
	/* get_recorded(): without the key that stopped the recording */
	long n = recordbuff.n - recordbuff_last;
	if (n < 0) n = 0;
	reg_recorded = reg_recording;
	reg_recording = 0;
	Reg *keep = y_previous;
	/* stuff_yank() */
	if (regname == '"') regname = 0;
	if (regname >= 'A' && regname <= 'Z') {
		Reg *y = &y_regs[op_reg_index(regname)];
		if (y->s && y->type == MCHAR) {
			Sbuf b = { 0 };
			bput(&b, y->s, y->n);
			bput(&b, recordbuff.s ? recordbuff.s : "", n);
			free(y->s);
			y->s = b.s;
			y->n = b.n;
			y_previous = keep;
			return 1;
		}
	}
	Reg *y = get_reg(regname, 1);
	free(y->s);
	y->s = malloc((size_t)n + 1);
	if (y->s) memcpy(y->s, recordbuff.s ? recordbuff.s : "", (size_t)n);
	y->n = n;
	y->type = MCHAR;
	y->width = 0;
	y_previous = keep;
	return 1;
}

int do_execreg(int regname)
{
	if (regname == '@') {
		if (execreg_lastc == 0) { core_msg(E, "E748: No previously used register"); return 0; }
		regname = execreg_lastc;
	}
	if (regname == '%' || regname == '#' || !valid_yank_reg(regname, 0)) { beep(); return 0; }
	execreg_lastc = regname;
	if (regname == '_') return 1;
	if (regname == ':') {
		if (!last_cmdline) { core_msg(E, "E30: No previous command line"); return 0; }
		Sbuf b = { 0 };
		bput(&b, ":", 1);
		bput(&b, last_cmdline, (long)strlen(last_cmdline));
		bput(&b, "\r", 1);
		stuff_bytes(b.s, b.n, 1, 0);
		free(b.s);
		E->ks.executing = regname;
		return 1;
	}
	if (regname == '.') {
		const int *k;
		long n = last_insert_keys(&k);
		if (n < 0) { core_msg(E, "E29: No inserted text yet"); return 0; }
		typebuf_ins(k, n);
		E->ks.executing = regname;
		return 1;
	}
	Reg *reg = get_reg(regname, 0);
	if (!reg->s) return 0;
	Sbuf b = { 0 };
	bput(&b, reg->s, reg->n);
	if (reg->type == MLINE) bput(&b, "\n", 1);
	stuff_bytes(b.s ? b.s : "", b.n, 1, 0);
	free(b.s);
	E->ks.executing = regname == 0 ? '"' : regname;
	return 1;
}

/* CTRL-R in Insert mode: the register's text, typed */
/* ins_reg() and insert_reg(): CTRL-R {register} in Insert mode, after
   CTRL-R, CTRL-O or CTRL-P when "literally" is one of them; 0 to beep */
int insert_reg(int regname, int literally)
{
	if (!valid_yank_reg(regname, 0)) return 0;
	int alnum = (regname >= '0' && regname <= '9') || (regname >= 'a' && regname <= 'z') || (regname >= 'A' && regname <= 'Z');
	if (literally == 15 || literally == 16 || (reg_lines(get_reg(regname, 0)) > 1 && (alnum || regname == '*' || regname == '+'))) {
		/* put, not typed: CTRL-R CTRL-O, CTRL-R CTRL-P, and the lines
		   of a literal register */
		redo_append(KEY_CTRL_R);
		if (literally == 15 || literally == 16) redo_append(literally);
		redo_append(regname);
		do_put(regname, BACKWARD, 1, PUT_CURSEND | (literally == 16 ? PUT_FIXINDENT : 0));
		return 1;
	}
	/* is_literal_register(): a named register goes in as text */
	literally = literally || alnum || regname == '*' || regname == '+';
	if (regname == '_') return 1;
	if (regname == '.') return stuff_inserted(0, 1, 1);
	if (regname == ':' || regname == '%') {
		const char *s = regname == ':' ? last_cmdline : B->path;
		if (!s) return 0;
		stuff_bytes(s, (long)strlen(s), 0, literally);
		return 1;
	}
	Reg *reg = get_reg(regname, 0);
	if (!reg->s) return 0;
	if (regname == '-' && reg->type == MCHAR) {
		/* the small delete register is put, not typed */
		int dir = BACKWARD;
		if (E->ks.replace) {
			/* Replace mode: the text it covers goes first */
			u_save_cursor();
			long l = (long)W->line, c = (long)W->col, k = 0;
			for (long i = 0; i < reg->n; i += clen(reg->s, reg->n, i)) {
				if (c + k >= ml_len(l)) break;
				k += clen(ml_get(l), ml_len(l), c + k);
			}
			replace_bytes(l, c, k, "", 0);
			Pos p = cur();
			if (!oneright()) dir = FORWARD;
			setcur(p);
		}
		redo_append(KEY_CTRL_R);
		redo_append('-');
		do_put('-', dir, 1, PUT_CURSEND);
		return 1;
	}
	/* each line escaped, the line breaks typed as Enter */
	for (long i = 0, j; i <= reg->n; i = j + 1) {
		for (j = i; j < reg->n && reg->s[j] != '\n'; j++) {}
		stuff_bytes(reg->s + i, j - i, 0, literally);
		if (j < reg->n || reg->type == MLINE) stuff_bytes("\n", 1, 0, 0);
	}
	return 1;
}

/* ---- gq and :Format ---- */

/* Davy's conform.nvim formatters_by_ft: 1 sql (sqldiff), 2 systemverilog
   and verilog (verible), 0 none */
int fmt_kind(void)
{
	const char *x = B->path ? strrchr(B->path, '.') : NULL;
	return !x ? 0 : !strcmp(x, ".sql") ? 1 : !strcmp(x, ".sv") || !strcmp(x, ".svh") || !strcmp(x, ".v") || !strcmp(x, ".vh") ? 2 : 0;
}

static Str vout;   /* verible's stdout */
static void on_verible(void *ctx, int stream, const char *b, size_t n)
{
	if (stream == 1 && b) *(int *)ctx |= str_add(&vout, b, n);
	if (!stream) *(int *)ctx |= n != 0;
}

/* conform.nvim's verible (its range_args, --lines), with Davy's 4-space
   indent: the whole text goes through verible-verilog-format, as
   'formatprg' through system() (proc_wait); the edit is where its output
   differs.  0 ok, -1 with the message set. */
static int verible(size_t *a, size_t *z, const char **res, size_t *n)
{
	char lines[48];
	size_t len = text_len(B->text), p = 0, q = 0;
	char *in = malloc(len + 1);
	if (!in) { core_msg(E, "E342: Out of memory!"); return -1; }
	text_read(B->text, 0, in, len);
	snprintf(lines, sizeof lines, "--lines=%zu-%zu", text_line_of(B->text, *a) + 1, text_line_of(B->text, *z > *a ? *z - 1 : *z) + 1);
	char *argv[] = { "verible-verilog-format", "--indentation_spaces=4", lines, "-", NULL };
	int bad = 0, id = proc_spawn(argv, NULL, on_verible, &bad);
	if (id >= 0) {
		vout.n = 0;
		bad |= proc_write(id, in, len);
		proc_close(id);
		bad |= proc_wait(id, 1000);
		while (p < len && p < vout.n && in[p] == vout.s[p]) p++;
		while (q < len - p && q < vout.n - p && in[len - 1 - q] == vout.s[vout.n - 1 - q]) q++;
	}
	free(in);
	if (id < 0 || bad) { core_msg(E, id < 0 ? "Cannot run verible-verilog-format" : "verible-verilog-format failed"); return -1; }
	*a = p; *z = len - q; *res = vout.s ? vout.s + p : ""; *n = vout.n - p - q;
	return 0;
}

/* The buffer's formatter over bytes [a, z) (sqlfmt's range; verible's
   lines): only the changes are applied */
int buf_format(size_t a, size_t z)
{
	const char *res;
	size_t n;
	int kept = 0;
	if (fmt_kind() == 2 ? verible(&a, &z, &res, &n) : (kept = sqlfmt(B->text, &a, &z, &res, &n)) < 0) {
		if (kept < 0) core_msg(E, "E342: Out of memory!");
		return -1;
	}
	if (z > a || n) {
		/* the cursor stays on its text: after the edit it moves with it,
		   inside it keeps its offset into the new text (as near as fits) */
		size_t p = core_line_start(E, W->line) + W->col;
		p = p >= z ? p - (z - a) + n : p > a ? a + (p - a < n ? p - a : n) : p;
		core_edit(E, a, z - a, res, n);
		buf_changed();
		W->line = text_line_of(B->text, p);
		W->col = p - core_line_start(E, W->line);
		check_cursor();
		W->col = (size_t)head(ml_get((long)W->line), (long)W->col);
	}
	if (kept) core_msg(E, "%d statement%s kept as written", kept, kept == 1 ? "" : "s");
	return 0;
}

/* fmt_check_par() without 'comments' (see do_join()): a blank line, or a
   nroff paragraph macro, is not part of a paragraph */
static int fmt_check_par(long l) { return linewhite(l) || startPS(l, 0, 0); }

/* utf_head_off() before byte c: head() would count the line each step */
static long gprev(const char *s, long n, long c) { return (long)uni_gprev((const unsigned char *)s, (size_t)n, (size_t)c); }

/* internal_format() for format_lines() ('formatoptions' tcqj, no comment
   leaders): while the line's last non-blank ends past textwidth, break it
   at the last blanks that start at or before textwidth (the first blanks
   when none do).  Neovim finds them scanning back from the line's end and
   moves the rest of the line with open_line(), which is quadratic in a
   long line (gqq on a 190 KB line: 4 s and 446 MB of undo, measured once);
   scanning forward to textwidth finds the same blanks, and replacing just
   them with a line break and open_line()'s indent is the same text */
static void internal_format(long tw)
{
	for (;;) {
		long l = (long)W->line, n = ml_len(l);
		const char *s = ml_get(l);
		long c = n ? gprev(s, n, n) : 0;   /* the last non-blank */
		while (c && (s[c] == ' ' || (s[c] >= 9 && s[c] <= 13))) c = gprev(s, n, c);
		/* get_nolist_virtcol() validates w_virtcol here, on the last
		   non-blank, where format_lines() put the cursor */
		W->col = (size_t)c;
		validate_virtcol();
		long wantcol = col_of_vcol(l, tw, 1), foundcol = 0;
		if (c == 0 || wantcol > c) return;
		/* blanks after a non-blank, so not the indent */
		for (long i = 0, prev = 0; i < c; i += clen(s, n, i)) {
			int w = whitechar(s, n, i);
			if (w && prev) {
				if (i > wantcol) { if (!foundcol) foundcol = i; break; }
				foundcol = i;
			}
			prev = !w;
		}
		/* no blanks: the line stays long, and w_virtcol what
		   coladvance(textwidth) made it (F, G and H1 of reviews/pr-67.md) */
		if (foundcol == 0) {
			if (vcol_of(l, wantcol) == tw) W->vc.v = (size_t)tw;
			else W->vc.ok = 0;
			return;
		}
		long q = foundcol, ind = get_indent(l);
		while (q < n && whitechar(s, n, q)) q++;
		if (!ind) ind = old_indent;   /* "^ CTRL-D" in the last insert */
		old_indent = 0;
		Sbuf b = { 0 };
		bput(&b, "\n", 1);
		bspaces(&b, ind);
		replace_bytes(l, foundcol, q - foundcol, b.s, b.n);
		free(b.s);
		W->line = (size_t)l + 1;
	}
}

/* format_lines() without 'comments' and with textwidth 0: the window's
   width less one, at most 79 (comp_textwidth()).  Each paragraph's lines
   are joined as J does and broken again by internal_format(); a line
   longer than three times that is formatted before the next join */
static void format_lines(long line_count)
{
	long tw = W->w - 1 < 79 ? W->w - 1 : 79, max_len = tw * 3, l = (long)W->line;
	int next_is_not_par = fmt_check_par(l), is_not_par = 0, advance = 1, need_set_indent = 1, force_format = 0;
	l--;
	for (long count = line_count; count != 0; count--) {
		if (advance) { l++; is_not_par = next_is_not_par; }
		next_is_not_par = count == 1 || l == nlines() - 1 || fmt_check_par(l + 1);
		advance = 1;
		int is_end_par = is_not_par || next_is_not_par;
		if (is_not_par) continue;
		if (is_end_par || force_format) {
			if (need_set_indent) set_indent(l, get_indent(l));
			W->line = (size_t)l;
			if (tw > 0) internal_format(tw);
			l = (long)W->line;
			need_set_indent = is_end_par;
			force_format = 0;
		}
		if (!is_end_par) {
			advance = 0;
			W->line = (size_t)l;
			do_join(2, 1);
			force_format = ml_len(l) > max_len;
		}
	}
	W->line = (size_t)l;
}

/* OP_FORMAT: a buffer with a formatter has Davy's 'formatexpr'
   (op_formatexpr()) over the operator's lines; other buffers op_format() */
void op_format(Oparg *oap)
{
	if (fmt_kind()) {
		buf_format(core_line_start(E, (size_t)oap->start.lnum), core_line_start(E, (size_t)oap->end.lnum + 1));
		return;
	}
	/* the cursor where gq was typed, so u puts it back there */
	setcur(oap->cursor_start);
	u_save((size_t)oap->start.lnum, (size_t)oap->end.lnum + 1);
	setcur(oap->start);
	format_lines(oap->line_count);
	/* the first non-blank of the last formatted line; after "Q}" the next */
	if (oap->end_adjusted && (long)W->line < nlines() - 1) W->line++;
	beginline(BL_WHITE | BL_FIX);
}
