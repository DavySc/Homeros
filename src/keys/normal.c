/* Normal and Visual mode: Neovim 0.12's normal.c and do_pending_operator()
   (ops.c), with FEATURES.md's settings.  Keys arrive one at a time; a
   command waits in pend[] until it is complete, and then runs as Neovim's
   normal_cmd() would have with all its keys read. */
#include <string.h>
#include <stdio.h>
#include "k.h"
#include "uni.h"

typedef struct {
	Oparg *oap;
	int cmdchar, nchar, extra_char;
	long opcount, count0, count1;
	int arg, retval;
	const char *searchbuf;   /* / and ?: their command line */
} Cmdarg;
enum { CA_COMMAND_BUSY = 1, CA_NO_ADJ_OP_END = 2 };

int VIsual_active, VIsual_mode;
Pos VIsual;
int finish_op;
static Oparg oa;
static long opcount;
static int VIsual_reselect, VIsual_mode_orig;
/* what gv selects again (curbuf->b_visual): its ends are the buffer's '<
   and '> marks, so the core moves them as mark_adjust() and
   mark_col_adjust() move vi_start and vi_end */
static struct { long coladd[2]; int mode; long curswant; } b_visual;
static Pos vis(int i) { Mark m = B->mark[MARK_LT + i]; return (Pos){ (long)m.line, (long)m.col, b_visual.coladd[i] }; }
static void vis_set(int i, Pos p) { B->mark[MARK_LT + i] = (Mark){ (size_t)p.lnum, (size_t)p.col, 1 }; b_visual.coladd[i] = p.coladd; }
/* what 1v and a repeated Visual operator select (resel_VIsual_*, redo_VIsual) */
static int resel_VIsual_mode;
static long resel_VIsual_vcol, resel_VIsual_line_count;
static struct { int mode; long vcol, line_count, count; int arg; } redo_VIsual;
static int redo_VIsual_busy;

Pos visual_end(void) { return vis(1); }
long visual_last_line(void) { return vis(0).lnum > vis(1).lnum ? vis(0).lnum : vis(1).lnum; }

static void do_pending_operator(Cmdarg *cap, long old_col);
static void nv_operator(Cmdarg *cap);

static long curswant(void) { return W->want == WANT_EOL ? MAXCOL : (long)W->want; }
static void set_want(long v) { W->want = v >= MAXCOL ? WANT_EOL : (size_t)v; set_curswant = 0; }
static void update_curswant_force(void) { set_curswant = 1; update_curswant(); }

static void clearop(Oparg *oap)
{
	oap->op_type = OP_NOP;
	oap->regname = 0;
	oap->motion_force = 0;
	oap->use_reg_one = 0;
}

static void clearopbeep(Oparg *oap) { clearop(oap); beep(); }

static int checkclearop(Oparg *oap)
{
	if (oap->op_type == OP_NOP) return 0;
	clearopbeep(oap);
	return 1;
}

static int checkclearopq(Oparg *oap)
{
	if (oap->op_type == OP_NOP && !VIsual_active) return 0;
	clearopbeep(oap);
	return 1;
}

/* ---- the redo buffer ('.') ---- */

/* ResetRedobuff() keeps the previous command (old_redobuff): "." after
   CTRL-O repeats that one, not the insert CTRL-O interrupted */
/* Neovim's redo buffer has no limit: "." after typing 10,000 characters
   types them all again; each buffer grows to the longest command, then
   stays */
static int *redobuf, *old_redobuf;
static long redo_n, old_redo_n, redo_cap, old_redo_cap;
int redo_busy;

void redo_reset(void)
{
	if (redo_busy) return;
	int *t = old_redobuf;
	long tc = old_redo_cap;
	old_redobuf = redobuf;
	old_redo_cap = redo_cap;
	old_redo_n = redo_n;
	redobuf = t;
	redo_cap = tc;
	redo_n = 0;
}

long redo_get(const int **k) { *k = redobuf; return redo_n; }

int redo_append(int k)
{
	if (redo_busy) return 0;
	if (redo_n == redo_cap) {
		long c = redo_cap ? 2 * redo_cap : 256;
		int *b = realloc(redobuf, sizeof(int) * (size_t)c);
		if (!b) return 0;   /* the command repeats cut short, as on any failed allocation */
		redobuf = b;
		redo_cap = c;
	}
	redobuf[redo_n++] = k;
	return 0;
}

void redo_add_keys(const int *k, long n) { while (n--) redo_append(*k++); }
void redo_add_str(const char *s) { while (*s) redo_append((unsigned char)*s++); }

void redo_num_append(long n)
{
	char b[24];
	int i = 0;
	do b[i++] = (char)('0' + n % 10); while (n /= 10);
	while (i) redo_append(b[--i]);
}

/* prep_redo(): the command that "." repeats, keys without their count */
void prep_redo(int regname, long num, int c1, int c2, int c3, int c4, int c5)
{
	if (redo_busy) return;
	redo_reset();
	if (regname) { redo_append('"'); redo_append(regname); }
	if (num) redo_num_append(num);
	int c[] = { c1, c2, c3, c4, c5 };
	for (int i = 0; i < 5; i++) if (c[i]) redo_append(c[i]);
}

static void prep_redo_cmd(Cmdarg *cap)
{
	prep_redo(cap->oap->regname, cap->count0, 0, cap->cmdchar, 0, 0, cap->nchar);
}

/* start_redo(): stuff the redo buffer, with the new count and the next
   numbered register */
static int start_redo(long count, int old_redo)
{
	const int *rb = old_redo ? old_redobuf : redobuf;
	long rn = old_redo ? old_redo_n : redo_n;
	if (!rn) return 0;
	int b[32];   /* register, v, count and command; the rest goes as it is */
	long n = 0, i = 0;
	int c = rb[i++];
	if (c == '"') {
		b[n++] = '"';
		c = rb[i++];
		if (c >= '1' && c < '9') c++;
		b[n++] = c;
		c = i < rn ? rb[i++] : 0;
	}
	if (c == 'v') {
		VIsual = cur();
		VIsual_active = 1;
		VIsual_reselect = 1;
		redo_VIsual_busy = 1;
		c = i < rn ? rb[i++] : 0;
	}
	if (count) {
		while (c >= '0' && c <= '9') c = i < rn ? rb[i++] : 0;
		char d[24];
		int k = 0;
		do d[k++] = (char)('0' + count % 10); while (count /= 10);
		while (k) b[n++] = d[--k];
	}
	b[n++] = c;
	stuff_redo(b, n);
	stuff_redo(rb + i, rn - i);
	return 1;
}

/* start_redo_ins(): an insert's count repeats what was typed, from the
   redo buffer after the command character */
void start_redo_ins(void)
{
	long i = 0;
	while (i < redo_n && !has("AaIiRrOo", redobuf[i])) i++;
	if (i >= redo_n) return;
	int c = redobuf[i++], nl = '\n';
	if (c == 'O' || c == 'o') stuff_redo(&nl, 1);
	stuff_redo(redobuf + i, redo_n - i);
	redo_busy = 1;            /* block_redo */
}

/* ---- Visual mode ---- */

void end_visual_mode(void)
{
	VIsual_active = 0;
	vis_set(0, VIsual);
	vis_set(1, cur());
	b_visual.mode = VIsual_mode;
	update_curswant();
	b_visual.curswant = curswant();
	Pos p = cur();
	p.coladd = 0;
	setcur(p);
	check_cursor_col();
}

static void n_start_visual_mode(int c)
{
	VIsual_mode = c;
	VIsual_active = 1;
	VIsual_reselect = 1;
	/* on a Tab a Visual block starts at the cell Normal mode showed, the
	   Tab's last (coladvance(w_virtcol) with the cached virtcol) */
	if (c == 22 && gchar_cursor() == '\t') coladvance(vcol_end((long)W->line, (long)W->col));
	VIsual = cur();
}

static void nv_right(Cmdarg *cap);
static void nv_down(Cmdarg *cap);

static void nv_visual(Cmdarg *cap)
{
	if (cap->oap->op_type != OP_NOP) {
		cap->oap->motion_force = cap->cmdchar;
		finish_op = 0;
		return;
	}
	if (VIsual_active) {
		if (VIsual_mode == cap->cmdchar) end_visual_mode();
		else VIsual_mode = cap->cmdchar;
		return;
	}
	if (cap->count0 > 0 && resel_VIsual_mode) {
		/* use the previously selected area, count times */
		VIsual = cur();
		VIsual_active = 1;
		VIsual_reselect = 1;
		if (resel_VIsual_mode != 'v' || resel_VIsual_line_count > 1) {
			W->line += (size_t)(resel_VIsual_line_count * cap->count0 - 1);
			check_cursor();
		}
		VIsual_mode = resel_VIsual_mode;
		if (VIsual_mode == 'v') {
			if (resel_VIsual_line_count <= 1) {
				update_curswant_force();
				set_want(curswant() + resel_VIsual_vcol * cap->count0 - 1);
			} else set_want(resel_VIsual_vcol);
			coladvance(curswant());
		}
		if (resel_VIsual_vcol == MAXCOL) {
			set_want(MAXCOL);
			coladvance(MAXCOL);
		} else if (VIsual_mode == 22) {
			long l = (long)W->line;
			W->line = (size_t)VIsual.lnum;
			update_curswant_force();
			set_want(curswant() + resel_VIsual_vcol * cap->count0 - 1);
			W->line = (size_t)l;
			coladvance(curswant());
		} else set_curswant = 1;
		return;
	}
	n_start_visual_mode(cap->cmdchar);
	if (cap->count0 > 0 && --cap->count1 > 0) {
		if (VIsual_mode == 'v' || VIsual_mode == 22) nv_right(cap);
		else if (VIsual_mode == 'V') nv_down(cap);
	}
}

static void nv_gv_cmd(Cmdarg *cap)
{
	(void)cap;
	if (!B->mark[MARK_LT].set || vis(0).lnum >= nlines()) { beep(); return; }
	Pos tpos;
	if (VIsual_active) {
		int i = VIsual_mode;
		VIsual_mode = b_visual.mode;
		b_visual.mode = i;
		update_curswant();
		long w = curswant();
		set_want(b_visual.curswant);
		b_visual.curswant = w;
		tpos = vis(1);
		Pos s = vis(0);
		vis_set(1, cur());
		vis_set(0, VIsual);
		setcur(s);
	} else {
		VIsual_mode = b_visual.mode;
		set_want(b_visual.curswant);
		tpos = vis(1);
		setcur(vis(0));
	}
	VIsual_active = 1;
	VIsual_reselect = 1;
	check_cursor();
	VIsual = cur();
	setcur(tpos);
	check_cursor();
}

static void v_swap_corners(int cmdchar)
{
	if (cmdchar == 'O' && VIsual_mode == 22) {
		Pos old = cur();
		long l1, r1, l2, r2;   /* getvcols(): with coladd */
		getvvcol(old, &l1, &r1);
		getvvcol(VIsual, &l2, &r2);
		long left = l1 < l2 ? l1 : l2, right = r1 > r2 ? r1 : r2;
		W->line = (size_t)VIsual.lnum;
		coladvance(left);
		VIsual = cur();
		W->line = (size_t)old.lnum;
		set_want(right);
		coladvance(right);
		if ((long)W->col == old.col && (!virtual_active() || cur().coladd == old.coladd)) {
			W->line = (size_t)VIsual.lnum;
			coladvance(right);
			VIsual = cur();
			W->line = (size_t)old.lnum;
			coladvance(left);
			set_want(left);
		}
		return;
	}
	Pos old = cur();
	setcur(VIsual);
	VIsual = old;
	set_curswant = 1;
}

/* ---- motions ---- */

/* adjust_cursor(): a motion may not leave the cursor on the end of line,
   then it becomes inclusive */
static void adjust_cursor(Oparg *oap)
{
	if (W->col > 0 && gchar_cursor() == 0 && !VIsual_active) {
		dec_cursor();
		oap->inclusive = 1;
	}
}

static void nv_right(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	oap->motion_type = MCHAR;
	oap->inclusive = 0;
	/* with 'virtualedit' lines are endless: no "past_line" */
	int past_line = VIsual_active && !virtual_active();
	long n;
	for (n = cap->count1; n > 0; n--) {
		if ((!past_line && !oneright()) || (past_line && gchar_cursor() == 0)) {
			/* whichwrap=b,s (Neovim's default): space wraps, l does not */
			if (cap->cmdchar == ' ' && (long)W->line + 1 < nlines()) {
				if (oap->op_type != OP_NOP && !oap->inclusive && !lineempty((long)W->line)) {
					oap->inclusive = 1;
				} else {
					setcur((Pos){ (long)W->line + 1, 0, 0 });
					set_curswant = 1;
					oap->inclusive = 0;
				}
				continue;
			}
			if (oap->op_type == OP_NOP) {
				if (n == cap->count1) beep();
			} else if (!lineempty((long)W->line)) oap->inclusive = 1;
			break;
		} else if (past_line) {
			set_curswant = 1;
			const char *s = ml_get((long)W->line);
			W->col += (size_t)clen(s, ml_len((long)W->line), (long)W->col);
		}
	}
}

static void nv_left(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	oap->motion_type = MCHAR;
	oap->inclusive = 0;
	for (long n = cap->count1; n > 0; n--) {
		if (!oneleft()) {
			/* whichwrap: backspace wraps */
			if ((cap->cmdchar == KEY_BS || cap->cmdchar == 8) && W->line > 0) {
				W->line--;
				coladvance(MAXCOL);
				set_curswant = 1;
				if ((oap->op_type == OP_DELETE || oap->op_type == OP_CHANGE) && !lineempty((long)W->line)) {
					if (gchar_cursor()) W->col += (size_t)clen(ml_get((long)W->line), ml_len((long)W->line), (long)W->col);
					cap->retval |= CA_NO_ADJ_OP_END;
				}
				continue;
			} else if (oap->op_type == OP_NOP && n == cap->count1) beep();
			break;
		}
	}
}

static void nv_down(Cmdarg *cap)
{
	cap->oap->motion_type = MLINE;
	if (!cursor_down(cap->count1)) clearopbeep(cap->oap);
	else if (cap->arg) beginline(BL_WHITE | BL_FIX);
}

static void nv_up(Cmdarg *cap)
{
	cap->oap->motion_type = MLINE;
	if (!cursor_up(cap->count1)) clearopbeep(cap->oap);
	else if (cap->arg) beginline(BL_WHITE | BL_FIX);
}

static void nv_dollar(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 1;
	set_want(MAXCOL);
	if (!cursor_down(cap->count1 - 1)) clearopbeep(cap->oap);
}

static void nv_beginline(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	beginline(cap->arg);
	ins_at_eol = 0;   /* "0" after CTRL-O stays on a one-character line's character */
}

static void nv_pipe(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	beginline(0);
	if (cap->count0 > 0) {
		coladvance(cap->count0 - 1);
		set_want(cap->count0 - 1);
	} else set_want(0);
}

static void nv_lineop(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	oap->motion_type = MLINE;
	if (!cursor_down(cap->count1 - 1)) clearopbeep(oap);
	else if ((oap->op_type == OP_DELETE && oap->motion_force != 'v' && oap->motion_force != 22)
	         || oap->op_type == OP_LSHIFT || oap->op_type == OP_RSHIFT)
		beginline(BL_SOL | BL_FIX);
	else if (oap->op_type != OP_YANK) beginline(BL_WHITE | BL_FIX);
}

static void nv_goto(Cmdarg *cap)
{
	long l = cap->arg ? nlines() : 1;
	cap->oap->motion_type = MLINE;
	if (cap->count0) l = cap->count0;
	if (l < 1) l = 1;
	if (l > nlines()) l = nlines();
	W->line = (size_t)(l - 1);
	beginline(BL_SOL | BL_FIX);
}

static void nv_wordcmd(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	int word_end = cap->cmdchar == 'e' || cap->cmdchar == 'E', flag = 0, n;
	Pos startpos = cur();
	oap->inclusive = word_end;
	/* "cw" is "ce" (when not on a blank), and changes one char at a word's end */
	if (!word_end && oap->op_type == OP_CHANGE) {
		n = gchar_cursor();
		if (n != 0 && n != ' ' && n != '\t') {
			oap->inclusive = 1;
			word_end = 1;
			flag = 1;
		}
	}
	oap->motion_type = MCHAR;
	set_curswant = 1;
	if (word_end) n = end_word(cap->count1, cap->arg, flag, 0);
	else n = fwd_word(cap->count1, cap->arg, oap->op_type != OP_NOP);
	if (lt(startpos, cur())) adjust_cursor(oap);
	if (!n && oap->op_type == OP_NOP) clearopbeep(oap);
}

static void nv_bck_word(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	set_curswant = 1;
	if (!bck_word(cap->count1, cap->arg, 0)) clearopbeep(cap->oap);
}

/* searchc() sets inclusive through this */
int searchc_inclusive;

static void nv_csearch(Cmdarg *cap)
{
	int t_cmd = cap->cmdchar == 't' || cap->cmdchar == 'T';
	cap->oap->motion_type = MCHAR;
	int dir = cap->cmdchar == ';' || cap->cmdchar == ',' ? 0 : cap->arg;
	if (cap->nchar >= KEY_UP
	    || !searchc(cap->nchar, dir, t_cmd, cap->count1, cap->cmdchar == ';' ? 1 : cap->cmdchar == ',' ? -1 : 0)) {
		clearopbeep(cap->oap);
		return;
	}
	cap->oap->inclusive = searchc_inclusive;
	set_curswant = 1;
	/* a Tab is included whole by "tx" and "dfx" in a Visual block; Neovim
	   tests cap->arg == FORWARD, which "," (arg true) also passes */
	Pos p = cur();
	p.coladd = 0;
	if (gchar_cursor() == '\t' && virtual_active() && (cap->cmdchar == ',' || (cap->cmdchar != ';' && cap->arg == FORWARD))
	    && (t_cmd || cap->oap->op_type != OP_NOP))
		p.coladd = vcol_end(p.lnum, p.col) - vcol_of(p.lnum, p.col);
	setcur(p);
}


static void nv_percent(Cmdarg *cap)
{
	cap->oap->inclusive = 1;
	if (cap->count0) {
		if (cap->count0 > 100) { clearopbeep(cap->oap); return; }
		cap->oap->motion_type = MLINE;
		long n = nlines(), l = (n * cap->count0 + 99) / 100;
		if (l < 1) l = 1;
		if (l > n) l = n;
		W->line = (size_t)(l - 1);
		beginline(BL_SOL | BL_FIX);
		return;
	}
	Pos p;
	cap->oap->motion_type = MCHAR;
	cap->oap->use_reg_one = 1;
	if (!findmatch(&p, 0)) clearopbeep(cap->oap);
	else { setcur(p); set_curswant = 1; }
}

static void coladd_reset(void) { Pos p = cur(); p.coladd = 0; setcur(p); }

static void nv_findpar(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	cap->oap->use_reg_one = 1;
	set_curswant = 1;
	if (!findpar(&cap->oap->inclusive, cap->arg, cap->count1, 0, 0)) { clearopbeep(cap->oap); return; }
	coladd_reset();
}

static void nv_brace(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->use_reg_one = 1;
	cap->oap->inclusive = 0;
	set_curswant = 1;
	if (!findsent(cap->arg, cap->count1)) { clearopbeep(cap->oap); return; }
	coladd_reset();
	adjust_cursor(cap->oap);
}

static void nv_object(Cmdarg *cap)
{
	int include = cap->cmdchar == 'a', flag;
	const char *mps_save = p_mps;
	p_mps = "(:),{:},[:],<:>,";
	switch (cap->nchar) {
	case 'w': flag = current_word(cap->oap, cap->count1, include, 0); break;
	case 'W': flag = current_word(cap->oap, cap->count1, include, 1); break;
	case 'b': case '(': case ')': flag = current_block(cap->oap, cap->count1, include, '(', ')'); break;
	case 'B': case '{': case '}': flag = current_block(cap->oap, cap->count1, include, '{', '}'); break;
	case '[': case ']': flag = current_block(cap->oap, cap->count1, include, '[', ']'); break;
	case '<': case '>': flag = current_block(cap->oap, cap->count1, include, '<', '>'); break;
	case 'p': flag = current_par(cap->oap, cap->count1, include); break;
	case 's': flag = current_sent(cap->oap, cap->count1, include); break;
	case '"': case '\'': case '`': flag = current_quote(cap->oap, cap->count1, include, cap->nchar); break;
	default: flag = 0;
	}
	p_mps = mps_save;
	if (!flag) clearopbeep(cap->oap);
	/* adjust_cursor_col() */
	if (W->col > 0 && (!VIsual_active) && gchar_cursor() == 0) dec_cursor();
	set_curswant = 1;
}

static void nv_brackets(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	oap->motion_type = MCHAR;
	oap->inclusive = 0;
	Pos old = cur();
	coladd_reset();   /* nv_brackets() does it even for an error */
	if ((cap->cmdchar == '[' && (cap->nchar == '{' || cap->nchar == '('))
	    || (cap->cmdchar == ']' && (cap->nchar == '}' || cap->nchar == ')'))) {
		/* "[{", "[(", "]}" or "])": go to the unmatched brace */
		int findc = cap->nchar;
		Pos p = old, found;
		int n;
		for (n = (int)cap->count1; n > 0; n--) {
			Pos save = cur();
			setcur(p);
			int ok = findmatchlimit(&found, findc, cap->cmdchar == '[' ? FM_BACKWARD : FM_FORWARD);
			setcur(save);
			if (!ok) {
				/* nv_bracket_block() puts back old_pos, coladd too */
				if (eq(p, old)) { setcur(old); clearopbeep(oap); return; }
				break;
			}
			p = found;
		}
		setcur(p);
		set_curswant = 1;
		return;
	}
	if (cap->nchar == '[' || cap->nchar == ']') {
		int flag = cap->nchar == cap->cmdchar ? '{' : '}';
		set_curswant = 1;
		if (!findpar(&oap->inclusive, cap->arg, cap->count1, flag,
		             oap->op_type != OP_NOP && cap->arg == FORWARD && flag == '{'))
			clearopbeep(oap);
		else if (oap->op_type == OP_NOP) beginline(BL_WHITE | BL_FIX);
		return;
	}
	if (cap->nchar == ' ') {
		/* Neovim 0.12's default mappings "[<Space>" and "]<Space>": count
		   empty lines above or below; the cursor stays on its text */
		if (checkclearopq(oap)) return;
		long n = cap->count1, l = (long)W->line;
		char *b = calloc((size_t)n, 1);
		if (!b) return;
		memset(b, '\n', (size_t)n - 1);
		Pos c = cur();
		append_lines(cap->cmdchar == '[' ? l - 1 : l, b, n - 1);
		free(b);
		if (cap->cmdchar == '[') c.lnum += n;
		setcur(c);
		return;
	}
	clearopbeep(oap);
}

/* ---- simple changes ---- */

static void nv_optrans(Cmdarg *cap)
{
	/* Neovim's default maps: Y is y$, & is :&& (keeping the flags) */
	static const char *ar[] = { "dl", "dh", "d$", "c$", "cl", "cc", "y$", ":&&\n" };
	static const char str[] = "xXDCsSY&";
	if (!checkclearopq(cap->oap)) {
		if (cap->count0) stuff_num(cap->count0);
		const char *a = ar[strchr(str, cap->cmdchar) - str];
		stuff_str(a, (long)strlen(a));
	}
	cap->opcount = 0;
}

static void v_visop(Cmdarg *cap)
{
	static const char trans[] = "YyDdCcxdXdAAIIrr";
	if (cap->cmdchar >= 'A' && cap->cmdchar <= 'Z') {
		if (VIsual_mode != 22) { VIsual_mode_orig = VIsual_mode; VIsual_mode = 'V'; }
		else if (cap->cmdchar == 'C' || cap->cmdchar == 'D') set_want(MAXCOL);
	}
	cap->cmdchar = (unsigned char)*(strchr(trans, cap->cmdchar) + 1);
	nv_operator(cap);
}

static void nv_abbrev(Cmdarg *cap)
{
	if (cap->cmdchar == KEY_DEL) cap->cmdchar = 'x';   /* without x's NV_KEEPREG */
	if (VIsual_active) v_visop(cap);
	else nv_optrans(cap);
}

static void nv_subst(Cmdarg *cap)
{
	if (VIsual_active) {
		if (cap->cmdchar == 'S') { VIsual_mode_orig = VIsual_mode; VIsual_mode = 'V'; }
		cap->cmdchar = 'c';
		nv_operator(cap);
	} else nv_optrans(cap);
}

static void nv_Replace(Cmdarg *cap)
{
	if (VIsual_active) {
		cap->cmdchar = 'c';
		cap->nchar = 0;
		VIsual_mode_orig = VIsual_mode;
		VIsual_mode = 'V';
		nv_operator(cap);
		return;
	}
	if (checkclearopq(cap->oap)) return;
	invoke_edit('R', cap->count1, 0);
}

/* "r": replace count characters (nv_replace()) */
static void nv_replace(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	/* a special key aborts; <BS> (127) is Neovim's K_BS */
	if (cap->nchar >= KEY_UP || cap->nchar == 127) { clearopbeep(cap->oap); return; }
	if (VIsual_active) { nv_operator(cap); return; }
	const char *s = ml_get((long)W->line);
	long len = ml_len((long)W->line), c = (long)W->col, chars = 0;
	for (long i = c; i < len; i += clen(s, len, i)) chars++;
	if (len - c < cap->count1 || chars < cap->count1) { clearopbeep(cap->oap); return; }
	/* with 'expandtab' a Tab goes through Replace mode */
	if (cap->nchar == '\t') {
		stuff_num(cap->count1);
		stuff_key('R');
		stuff_key('\t');
		stuff_key(KEY_ESC);
		return;
	}
	u_save_cursor();
	if (cap->nchar == '\r' || cap->nchar == '\n') {
		long z = c;
		for (long i = 0; i < cap->count1; i++) z += clen(s, len, z);
		replace_bytes((long)W->line, c, z - c, "", 0);
		stuff_key('\r');
		stuff_key(KEY_ESC);
		invoke_edit('r', cap->count1, 1);
		return;
	}
	prep_redo(cap->oap->regname, cap->count1, 0, 'r', 0, 0, 0);
	redo_append(cap->nchar);
	char b[4];
	long nb = 0;
	{
		unsigned k = (unsigned)cap->nchar;
		if (k < 0x80) b[nb++] = (char)k;
		else if (k < 0x800) { b[nb++] = (char)(0xC0 + k / 64); b[nb++] = (char)(0x80 + k % 64); }
		else if (k < 0x10000) { b[nb++] = (char)(0xE0 + k / 4096); b[nb++] = (char)(0x80 + k / 64 % 64); b[nb++] = (char)(0x80 + k % 64); }
		else { b[nb++] = (char)(0xF0 + k / 262144); b[nb++] = (char)(0x80 + k / 4096 % 64); b[nb++] = (char)(0x80 + k / 64 % 64); b[nb++] = (char)(0x80 + k % 64); }
	}
	long z = c;
	for (long i = 0; i < cap->count1; i++) z += clen(s, len, z);
	char *r = malloc((size_t)nb * (size_t)cap->count1);
	if (!r) return;
	for (long i = 0; i < cap->count1; i++) memcpy(r + i * nb, b, (size_t)nb);
	replace_bytes((long)W->line, c, z - c, r, nb * cap->count1);
	free(r);
	W->col = (size_t)(c + nb * (cap->count1 - 1));
	set_curswant = 1;
	set_last_insert(cap->nchar);
}

static void n_swapchar(Cmdarg *cap)
{
	extern int swapchar(int op_type, Pos *pos);
	if (checkclearopq(cap->oap)) return;
	if (lineempty((long)W->line)) { clearopbeep(cap->oap); return; }
	prep_redo_cmd(cap);
	u_save_cursor();
	for (long n = cap->count1; n > 0; n--) {
		Pos p = cur();
		swapchar(cap->oap->op_type, &p);
		inc_cursor();
		if (gchar_cursor() == 0) break;
	}
	check_cursor();
	set_curswant = 1;
}

static void nv_tilde(Cmdarg *cap)
{
	if (!VIsual_active && cap->oap->op_type != OP_TILDE) n_swapchar(cap);
	else nv_operator(cap);
}

static void nv_join(Cmdarg *cap)
{
	if (VIsual_active) { nv_operator(cap); return; }
	if (checkclearop(cap->oap)) return;
	if (cap->count0 < 2) cap->count0 = 2;
	if ((long)W->line + cap->count0 > nlines()) {
		if (cap->count0 <= 2) { clearopbeep(cap->oap); return; }
		cap->count0 = nlines() - (long)W->line;
	}
	prep_redo(cap->oap->regname, cap->count0, 0, cap->cmdchar, 0, 0, cap->nchar);
	do_join(cap->count0, cap->nchar == 0);
}

static void nv_put(Cmdarg *cap, int fix_indent)
{
	extern Pos b_op_start, b_op_end;
	int dir, flags = 0, was_visual = 0, empty = 0;
	if (cap->oap->op_type != OP_NOP) { clearopbeep(cap->oap); return; }
	if (fix_indent) {
		dir = cap->cmdchar == ']' && cap->nchar == 'p' ? FORWARD : BACKWARD;
		flags |= PUT_FIXINDENT;
	} else dir = cap->cmdchar == 'P' || (cap->cmdchar == 'g' && cap->nchar == 'P') ? BACKWARD : FORWARD;
	prep_redo_cmd(cap);
	if (cap->cmdchar == 'g') flags |= PUT_CURSEND;
	Reg save = { 0 }, *saved = NULL;
	if (VIsual_active) {
		was_visual = 1;
		int regname = cap->oap->regname;
		int keep = cap->cmdchar == 'P';
		/* the register is copied before the selection is deleted into it */
		if (regname == 0 || regname == '"' || (regname >= '0' && regname <= '9') || regname == '-') {
			Reg *r = get_reg(regname, 0);
			if (r && r->s) {
				save = *r;
				save.s = malloc((size_t)r->n + 1);
				if (save.s) { memcpy(save.s, r->s, (size_t)r->n); saved = &save; }
			} else saved = &save;
		}
		cap->cmdchar = 'd';
		cap->nchar = 0;
		cap->oap->regname = keep ? '_' : 0;
		nv_operator(cap);
		do_pending_operator(cap, 0);
		empty = is_empty_buf();
		cap->oap->regname = regname;
		if (VIsual_mode == 'V') flags |= PUT_LINE;
		else if (VIsual_mode == 'v') flags |= PUT_LINE_SPLIT;
		if (VIsual_mode == 22 && dir == FORWARD) flags |= PUT_LINE_FORWARD;
		dir = BACKWARD;
		if ((VIsual_mode != 'V' && (long)W->col < b_op_start.col)
		    || (VIsual_mode == 'V' && (long)W->line < b_op_start.lnum))
			dir = FORWARD;
		VIsual_active = 1;
	}
	if (saved) {
		extern Reg *put_override;
		put_override = saved;
	}
	do_put(cap->oap->regname, dir, cap->count1, flags);
	if (saved) {
		extern Reg *put_override;
		put_override = NULL;
		free(save.s);
	}
	if (was_visual) {
		vis_set(0, b_op_start);
		vis_set(1, b_op_end);
	}
	/* all lines were selected and deleted: do_put() left an empty line */
	if (empty && lineempty(nlines() - 1)) {
		del_lines(nlines() - 1, 1);
		if ((long)W->line >= nlines()) {
			W->line = (size_t)nlines() - 1;
			coladvance(MAXCOL);
		}
	}
}

static void nv_addsub(Cmdarg *cap)
{
	if (!VIsual_active && cap->oap->op_type == OP_NOP) {
		prep_redo_cmd(cap);
		cap->oap->op_type = cap->cmdchar == 1 ? OP_NR_ADD : OP_NR_SUB;
		op_addsub(cap->oap, cap->count1, cap->arg);
		cap->oap->op_type = OP_NOP;
	} else if (VIsual_active) nv_operator(cap);
	else clearop(cap->oap);
}

static void nv_edit(Cmdarg *cap)
{
	if (VIsual_active && (cap->cmdchar == 'A' || cap->cmdchar == 'I')) { v_visop(cap); return; }
	if ((cap->cmdchar == 'a' || cap->cmdchar == 'i') && (cap->oap->op_type != OP_NOP || VIsual_active)) {
		nv_object(cap);
		return;
	}
	if (checkclearopq(cap->oap)) return;
	switch (cap->cmdchar) {
	case 'A': W->col = (size_t)ml_len((long)W->line); set_curswant = 1; break;
	case 'I': beginline(BL_WHITE); break;
	case 'a': if (gchar_cursor()) inc_cursor(); break;
	}
	invoke_edit(cap->cmdchar, cap->count1, 0);
}

static void nv_open(Cmdarg *cap)
{
	if (VIsual_active) { v_swap_corners(cap->cmdchar); return; }
	if (checkclearopq(cap->oap)) return;
	if (open_cmd(cap->cmdchar == 'O' ? BACKWARD : FORWARD)) invoke_edit(cap->cmdchar, cap->count1, 0);
}

/* nv_Undo(): "gUU" is "gUgU" and Visual U is gU; U on its own (undo line)
   is not in FEATURES.md, so it beeps */
static void nv_Undo(Cmdarg *cap)
{
	if (cap->oap->op_type == OP_UPPER || VIsual_active) {
		cap->cmdchar = 'g';
		cap->nchar = 'U';
		nv_operator(cap);
		return;
	}
	clearopbeep(cap->oap);
}

static void nv_undo(Cmdarg *cap)
{
	/* CTRL-R is nv_redo_or_register(): never an operator */
	if (cap->cmdchar == 'u' && (cap->oap->op_type == OP_LOWER || VIsual_active)) {
		cap->cmdchar = 'g';
		cap->nchar = 'u';
		nv_operator(cap);
		return;
	}
	if (checkclearopq(cap->oap)) return;
	/* nv_kundo() recomputes curswant from the cached screen column, which
	   the core keeps across the undo unless a step invalidated it
	   (core_set_want, Win.vc) */
	/* in a CTRL-O command that column is an Insert mode one (a Tab's first
	   cell, not its last), so restarting the insert does not step past a
	   Tab (measured: "\t", i y C-o u x puts x before it) */
	int mode = E->mode;
	if (restart_edit) E->mode = MODE_INSERT;
	size_t w0 = W->want;
	core_set_want(E);
	W->want = w0;
	/* u_doit() stops at the first step that finds nothing, with
	   beep_flush(), which ends a macro; in a CTRL-O command
	   check_cursor_col() lets the cursor stay past the end (restart_edit),
	   which core_clamp() allows in Insert mode */
	for (long n = cap->count1; n--;)
		if (core_undo(E, cap->cmdchar == KEY_CTRL_R) < 0) { beep(); break; }
	buf_changed();
	core_set_want(E);
	E->mode = mode;
	set_curswant = 0;
}

static void nv_dot(Cmdarg *cap)
{
	if (checkclearopq(cap->oap)) return;
	if (!start_redo(cap->count0, restart_edit && !arrow_used)) clearopbeep(cap->oap);
}

static void nv_regname(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	if (cap->nchar && valid_yank_reg(cap->nchar, 0)) {
		cap->oap->regname = cap->nchar;
		cap->opcount = cap->count0;
	} else clearopbeep(cap->oap);
}

extern int reg_recording, reg_recorded;
int  do_record(int c);
int  do_execreg(int regname);

static void nv_record(Cmdarg *cap)
{
	/* "gqq" is "gqgq" */
	if (cap->oap->op_type == OP_FORMAT) { cap->cmdchar = 'g'; cap->nchar = 'q'; nv_operator(cap); return; }
	if (checkclearop(cap->oap)) return;
	if (cap->nchar == ':' || cap->nchar == '/' || cap->nchar == '?') { beep(); return; }
	if (!reg_executing && !do_record(cap->nchar)) clearopbeep(cap->oap);
}

static void nv_at(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	while (cap->count1--)
		if (!do_execreg(cap->nchar)) { clearopbeep(cap->oap); break; }
}

static void nv_regreplay(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	while (cap->count1--)
		if (!do_execreg(reg_recorded)) { clearopbeep(cap->oap); break; }
}

static void nv_esc(Cmdarg *cap)
{
	int no_reason = cap->oap->op_type == OP_NOP && cap->opcount == 0 && cap->count0 == 0 && cap->oap->regname == 0;
	if (cap->arg) restart_edit = 0;
	if (VIsual_active) {
		end_visual_mode();
		check_cursor_col();
		set_curswant = 1;
	} else if (no_reason && !cap->arg) beep();
	clearop(cap->oap);
}

static void cmdline(int type, const char *range)
{
	E->mode = MODE_CMDLINE;
	E->cmdtype = type;
	E->cmdpos = E->cmdlen = strlen(range);
	memcpy(E->cmd, range, E->cmdlen);
}

/* Visual mode is the operator ":" (op_colon() puts '<,'> on the line), a
   count the range .,.+N-1 */
static void nv_colon(Cmdarg *cap)
{
	char r[32];
	if (VIsual_active) cap->oap->op_type = OP_COLON;
	else if (cap->oap->op_type != OP_NOP) clearopbeep(cap->oap);   /* ":" as a motion is not built */
	else if (cap->count0 > 1) snprintf(r, sizeof r, ".,.+%ld", cap->count0 - 1), cmdline(':', r);
	else cmdline(':', cap->count0 ? "." : "");
}

/* normal_search(): 0 not found, 1 found, 2 with a line offset */
static int normal_search(Cmdarg *cap, int dir, const char *pat, int rev, int *wrapped)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	cap->oap->use_reg_one = 1;
	set_curswant = 1;
	int i = do_search(cap->oap, dir, pat, cap->count1, rev, wrapped);
	if (!i) clearop(cap->oap);
	else if (i == 2) cap->oap->motion_type = MLINE;
	check_cursor();
	return i;
}

/* the line was read by the command line (run() waits for it) */
static void nv_search(Cmdarg *cap)
{
	static char buf[sizeof E->cmd + 1];
	memcpy(buf, E->cmd, E->cmdlen);
	buf[E->cmdlen] = 0;
	cap->searchbuf = buf;
	normal_search(cap, cap->cmdchar, buf, 0, NULL);
}

static void nv_next(Cmdarg *cap)
{
	Pos old = cur();
	int wrapped = 0;
	/* an offset can leave the cursor where it was: one match further */
	if (normal_search(cap, 0, NULL, cap->arg, &wrapped) == 1 && !wrapped && eq(old, cur())) {
		cap->count1++;
		normal_search(cap, 0, NULL, cap->arg, NULL);
		cap->count1--;
	}
}

/* * # g* g#: the keyword under or after the cursor, else the non-blank
   string (find_ident_at_pos() with FIND_IDENT|FIND_STRING) */
static void nv_ident(Cmdarg *cap)
{
	int g = cap->cmdchar == 'g', c = g ? cap->nchar : cap->cmdchar, i, cls = 0;
	long l = (long)W->line, n = ml_len(l), col = 0;
	const char *s = ml_get(l);
	for (i = 0; i < 2; i++) {
		for (col = (long)W->col; col < n; col += clen(s, n, col))
			if ((cls = cls_of(gchar((Pos){ l, col, 0 }))) && (i == 1 || cls != 1)) break;
		cls = col < n ? cls_of(gchar((Pos){ l, col, 0 })) : 0;
		while (col > 0 && cls) {
			long p = head(s, col - 1);
			if (cls_of(gchar((Pos){ l, p, 0 })) != cls) break;
			col = p;
		}
		if (cls > 2) cls = 2;
		if (cls == 2) break;
	}
	if (col >= n) { core_msg(E, "E348: No string under cursor"); clearop(cap->oap); return; }
	long z = col;
	int first = cls_of(gchar((Pos){ l, col, 0 }));
	while (z < n && (i == 0 ? cls_of(gchar((Pos){ l, z, 0 })) == first : cls_of(gchar((Pos){ l, z, 0 })) != 0)) z += clen(s, n, z);
	W->col = (size_t)col;
	char buf[sizeof E->cmd * 2 + 8], *p = buf;
	if (!g && first >= 2) p += sprintf(p, "\\<");
	for (long k = col; k < z && p - buf < (long)sizeof buf - 8; k++) {
		if (has(c == '*' ? "/.*~[^$\\" : "/?.*~[^$\\", (unsigned char)s[k])) *p++ = '\\';
		*p++ = s[k];
	}
	if (!g && cls_of(gchar((Pos){ l, head(s, z - 1), 0 })) >= 2) p += sprintf(p, "\\>");
	*p = 0;
	no_smartcase = 1;
	normal_search(cap, c == '*' ? '/' : '?', buf, 0, NULL);
}

/* ---- operators ---- */

static int get_op_type(int c1, int c2)
{
	switch (c1) {
	case 'd': return OP_DELETE;
	case 'y': return OP_YANK;
	case 'c': return OP_CHANGE;
	case '<': return OP_LSHIFT;
	case '>': return OP_RSHIFT;
	case '~': return OP_TILDE;
	case 'J': return OP_JOIN;
	case 'r': return OP_REPLACE;
	case 'I': return OP_INSERT;
	case 'A': return OP_APPEND;
	case 1: return OP_NR_ADD;
	case 24: return OP_NR_SUB;
	case 'g':
		switch (c2) {
		case '~': return OP_TILDE;
		case 'u': return OP_LOWER;
		case 'U': return OP_UPPER;
		case 'J': return OP_JOIN_NS;
		case 'q': return OP_FORMAT;
		}
	}
	return -1;
}

static int get_op_char(int op)
{
	static const int c[] = { 0, 'd', 'y', 'c', '<', '>', 'g', 'g', 'g', 'J', 'g', 'r', 'I', 'A', 1, 24, 'g' };
	return c[op];
}

static int get_extra_op_char(int op)
{
	switch (op) {
	case OP_TILDE: return '~';
	case OP_UPPER: return 'U';
	case OP_LOWER: return 'u';
	case OP_JOIN_NS: return 'J';
	case OP_FORMAT: return 'q';
	}
	return 0;
}

static void nv_operator(Cmdarg *cap)
{
	int op = get_op_type(cap->cmdchar, cap->nchar);
	if (op < 0) { clearopbeep(cap->oap); return; }
	if (op == cap->oap->op_type) nv_lineop(cap);
	else if (!checkclearop(cap->oap)) {
		cap->oap->start = cur();
		cap->oap->op_type = op;
	}
}

static void nv_g_cmd(Cmdarg *cap)
{
	Oparg *oap = cap->oap;
	switch (cap->nchar) {
	case 1: case 24:
		if (VIsual_active) {
			cap->arg = 1;
			cap->cmdchar = cap->nchar;
			cap->nchar = 0;
			nv_addsub(cap);
		} else clearopbeep(oap);
		break;
	case 'v': nv_gv_cmd(cap); break;
	case 'j': case KEY_DOWN: case 'k': case KEY_UP: {
		int up = cap->nchar == 'k' || cap->nchar == KEY_UP;
		if (W->wrap) {          /* nv_screengo(): the core moves by screen lines */
			oap->motion_type = MCHAR;
			oap->inclusive = W->want == WANT_EOL;
			if (core_screen_move(E, up ? -cap->count1 : cap->count1) < 0) clearopbeep(oap);
		} else {                /* 'nowrap': screen lines are text lines */
			oap->motion_type = MLINE;
			if (!(up ? cursor_up(cap->count1) : cursor_down(cap->count1))) clearopbeep(oap);
		}
		break;
	}
	case 'J': nv_join(cap); break;
	case '_': {
		/* g_: the last non-blank, count - 1 lines down */
		oap->motion_type = MCHAR;
		oap->inclusive = 1;
		set_want(MAXCOL);
		if (!cursor_down(cap->count1 - 1)) { clearopbeep(oap); break; }
		const char *s = ml_get((long)W->line);
		/* nv_g_underscore_cmd: from the last character step back by bytes
		   while on a blank, then mb_adjust_cursor; a blank under a
		   combining mark is skipped, the mark's bytes are not blanks;
		   in Visual mode coladvance(MAXCOL) is past the line ('selection'
		   inclusive), so the step back starts at its last byte */
		long n = ml_len((long)W->line), i = !n ? 0 : VIsual_active ? n - 1 : head(s, n - 1);
		while (i > 0 && (s[i] == ' ' || s[i] == '\t')) i--;
		W->col = (size_t)head(s, i);
		set_curswant = 1;
		break;
	}
	case 'e': case 'E':
		oap->motion_type = MCHAR;
		set_curswant = 1;
		oap->inclusive = 1;
		if (!bckend_word(cap->count1, cap->nchar == 'E', 0)) clearopbeep(oap);
		break;
	case 'I':
		beginline(0);
		if (!checkclearopq(oap)) {
			invoke_edit('g', cap->count1, 0);
		}
		break;
	/* Davy (2026-11-17): go is gg everywhere, as noremap go gg; Neovim's
	   go (byte N) is not kept */
	case 'g': case 'o': cap->arg = 0; nv_goto(cap); break;
	case 'q': cap->oap->cursor_start = cur(); /* fall through */
	case '~': case 'u': case 'U': nv_operator(cap); break;
	case 'p': case 'P': nv_put(cap, 0); break;
	case '*': case '#': nv_ident(cap); break;
	/* do_cmdline_cmd("%s//~/&"): not a typed command line (no history, no
	   ": register), and a pending operator then takes the cursor's move */
	case '&': { char a[] = "//~/&"; ex_sub(0, nlines() - 1, 's', a); break; }
	default: clearopbeep(oap);
	}
}

/* ---- the command table ---- */

enum { NCH = 1, NCH_NOP = 2 | NCH, NCH_ALW = 4 | NCH, KEEPREG = 8 };

/* ---- scrolling and windows: the core moves the view (core.h) ---- */

/* pagescroll() beeps when it cannot move, which ends a macro */
static void nv_halfpage(Cmdarg *cap)
{
	if (!checkclearop(cap->oap) && core_scroll_half(E, cap->cmdchar == 4 ? 1 : -1, cap->count0) < 0) beep();
}

/* CTRL-F and CTRL-B (nv_page()) */
static void nv_page(Cmdarg *cap)
{
	if (!checkclearop(cap->oap) && core_scroll_page(E, cap->cmdchar == 6 ? 1 : -1, cap->count1) < 0) beep();
}

static void nv_scroll_line(Cmdarg *cap)
{
	if (!checkclearop(cap->oap)) core_scroll(E, cap->cmdchar == 5 ? cap->count1 : -cap->count1);
}

static void nv_scroll(Cmdarg *cap)
{
	cap->oap->motion_type = MLINE;
	/* nv_scroll(): no cursor_correct() while an operator is pending, so
	   d L deletes to the last line shown */
	W->line = core_screen_line(E, cap->cmdchar, cap->count1, cap->oap->op_type == OP_NOP);
	beginline(BL_SOL | BL_FIX);
}

/* zt zz zb only (FEATURES.md); nv_zet() beeps for what it does not know */
static void nv_zet(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	if (cap->nchar == 't' || cap->nchar == 'z' || cap->nchar == 'b') core_scroll_cursor(E, cap->nchar);
	else clearopbeep(cap->oap);
}

/* do_window(): the CTRL-W keys FEATURES.md lists */
static void nv_window(Cmdarg *cap)
{
	int c = cap->nchar;
	long n = cap->count0, n1 = n ? n : 1;
	if (c == ':') { cap->cmdchar = ':'; cap->nchar = 0; nv_colon(cap); return; }
	if (checkclearop(cap->oap)) return;
	int one = E->wins->next == NULL, r = 0;
	Win *ow = E->win;
	Pos oc = cur();
	/* do_window(): these stop Visual mode first (reset_VIsual_and_resel()) */
	if (has("sSvnqcorR", c) || c == 19 || c == 22 || c == 14 || c == 17 || c == 3 || c == 15 || c == 18) {
		if (VIsual_active) end_visual_mode();
		VIsual_reselect = 0;
	}
	switch (c) {
	case 's': case 'S': case 19: r = core_split(E, 0, (int)n); break;
	case 'v': case 22: r = core_split(E, 1, (int)n); break;
	case 'n': case 14: r = core_new(E, 0); break;
	case 'q': case 17: r = core_close(E, 0); break;
	/* :close never closes the last window (E444) */
	case 'c': case 3: r = one ? -1 : core_close(E, 0); break;
	case 'o': case 15: r = core_only(E); break;
	case 'w': case 23: case 'W': r = one && n != 1 ? -1 : core_win_goto(E, c == 'W' ? 'W' : 'w', n); break;
	/* win_goto_ver() and win_goto_hor() stay put without a beep */
	case 'j': case KEY_DOWN: case 10: core_win_goto(E, 'j', n1); break;
	case 'k': case KEY_UP: case 11: core_win_goto(E, 'k', n1); break;
	case 'h': case KEY_LEFT: case 8: case KEY_BS: core_win_goto(E, 'h', n1); break;
	case 'l': case KEY_RIGHT: case 12: core_win_goto(E, 'l', n1); break;
	case 't': case 20: core_win_goto(E, 't', n1); break;
	case 'b': case 2: core_win_goto(E, 'b', n1); break;
	case 'p': case 16: r = core_win_goto(E, 'p', n1); break;
	case 'x': case 24: r = core_win_move(E, 'x', n); break;
	case 'r': case 18: r = core_win_move(E, 'r', n1); break;
	case 'R': r = core_win_move(E, 'R', n1); break;
	case 'H': case 'J': case 'K': case 'L': r = one ? -1 : core_win_move(E, c, n); break;
	case '=': core_equal(E); break;
	case '+': core_resize(E, 0, n1, 0); break;
	case '-': core_resize(E, 0, -n1, 0); break;
	case '_': case 31: core_resize(E, 0, n, 1); break;
	case '>': core_resize(E, 1, n1, 0); break;
	case '<': core_resize(E, 1, -n1, 0); break;
	case '|': core_resize(E, 1, n, 1); break;
	default: r = -1;
	}
	/* win_goto() and win_exchange(): Visual mode ends in the window left when the buffer
	   changes; on the same buffer the new window takes the cursor */
	if (VIsual_active && E->win != ow) {
		if (E->win->buf != ow->buf) {
			Win *nw = E->win;
			E->win = ow; E->buf = ow->buf;
			end_visual_mode();
			VIsual_reselect = 0;
			E->win = nw; E->buf = nw->buf;
		} else setcur(oc);
	}
	if (r < 0) beep();
}

static int cmd_flags(int c)
{
	switch (c) {
	case '"': return NCH_NOP | KEEPREG;
	case '\'': case '`': case 'F': case 'T': case '[': case ']': case 'f': case 'g': case 't': case 'z': return NCH_ALW;
	case '@': case 'm': case 'r': return NCH_NOP;
	case 'a': case 'i': case 'q': return NCH;
	case 'C': case 'D': case 'S': case 'X': case 'Y': case 's': case 'x': case '.': return KEEPREG;
	}
	return 0;
}

static int known(int c)
{
	return has(" \"#$%&'()*+,-./0:;<>?@ABCDEFGHIJLMNOPQRSTUWXYV[]^_`abcdefghijklmnopqrstuvwxyz{|}~", c) || c == 1 || c == 3
	       || c == 8 || c == 10 || c == 13 || c == 14 || c == 16 || c == 22 || c == 24 || c == KEY_CTRL_R
	       || c == 4 || c == 12 || c == 21 || c == 6 || c == 2 || c == 5 || c == 25 || c == 23
	       || c == KEY_ESC || c == KEY_BS || c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT || c == KEY_DEL;
}

static void execute(Cmdarg *cap)
{
	int c = cap->cmdchar;
	switch (c) {
	case 1: case 24: nv_addsub(cap); break;
	case 3: cap->arg = 1; nv_esc(cap); break;
	case KEY_ESC: cap->arg = 0; nv_esc(cap); break;
	case 8: case KEY_BS: case 'h': case KEY_LEFT: nv_left(cap); break;
	case ' ': case 'l': case KEY_RIGHT: nv_right(cap); break;
	case 10: case 14: case 'j': case KEY_DOWN: cap->arg = 0; nv_down(cap); break;
	case 13: case '+': cap->arg = 1; nv_down(cap); break;
	case 16: case 'k': case KEY_UP: cap->arg = 0; nv_up(cap); break;
	case '-': cap->arg = 1; nv_up(cap); break;
	case KEY_CTRL_R: case 'u': nv_undo(cap); break;
	case 22: case 'v': case 'V': nv_visual(cap); break;
	case '"': nv_regname(cap); break;
	case '$': nv_dollar(cap); break;
	case '%': nv_percent(cap); break;
	case '(': cap->arg = BACKWARD; nv_brace(cap); break;
	case ')': cap->arg = FORWARD; nv_brace(cap); break;
	case ',': case ';': nv_csearch(cap); break;
	case '.': nv_dot(cap); break;
	case '0': cap->arg = 0; nv_beginline(cap); break;
	case '^': cap->arg = BL_WHITE | BL_FIX; nv_beginline(cap); break;
	case ':': nv_colon(cap); break;
	case '/': case '?': nv_search(cap); break;
	case 'n': case 'N': cap->arg = c == 'N'; nv_next(cap); break;
	case '*': case '#': nv_ident(cap); break;
	case '<': case '>': case 'c': case 'd': case 'y': nv_operator(cap); break;
	case '@': nv_at(cap); break;
	case 'A': case 'I': case 'a': case 'i': nv_edit(cap); break;
	case 'B': cap->arg = 1; nv_bck_word(cap); break;
	case 'b': cap->arg = 0; nv_bck_word(cap); break;
	case 'C': case 'D': case 'X': case 'Y': case 'x': case KEY_DEL: nv_abbrev(cap); break;
	case '&': nv_optrans(cap); break;
	case 'E': case 'W': cap->arg = 1; nv_wordcmd(cap); break;
	case 'e': case 'w': cap->arg = 0; nv_wordcmd(cap); break;
	case 'F': case 'T': cap->arg = BACKWARD; nv_csearch(cap); break;
	case 'f': case 't': cap->arg = FORWARD; nv_csearch(cap); break;
	case 'G': cap->arg = 1; nv_goto(cap); break;
	case 'J': nv_join(cap); break;
	case 'O': case 'o': nv_open(cap); break;
	case 'P': case 'p': nv_put(cap, 0); break;
	case 'Q': nv_regreplay(cap); break;
	case 'U': nv_Undo(cap); break;
	case 'R': cap->arg = 0; nv_Replace(cap); break;
	case 'S': case 's': nv_subst(cap); break;
	case '[': cap->arg = BACKWARD; nv_brackets(cap); break;
	case ']': cap->arg = FORWARD; nv_brackets(cap); break;
	case '_': nv_lineop(cap); break;
	case 'g': nv_g_cmd(cap); break;
	case 'q': nv_record(cap); break;
	case 'r': nv_replace(cap); break;
	case '{': cap->arg = BACKWARD; nv_findpar(cap); break;
	case '}': cap->arg = FORWARD; nv_findpar(cap); break;
	case '|': nv_pipe(cap); break;
	case '~': nv_tilde(cap); break;
	case 4: case 21: nv_halfpage(cap); break;
	case 6: case 2: nv_page(cap); break;
	case 5: case 25: nv_scroll_line(cap); break;
	case 'H': case 'L': case 'M': nv_scroll(cap); break;
	case 'z': nv_zet(cap); break;
	case 23: nv_window(cap); break;
	/* nv_clear(), and Neovim's default map: CTRL-L is :nohlsearch first */
	case 12: if (!checkclearop(cap->oap) && !VIsual_active) no_hlsearch = 1; break;
	/* marks come with the core's mark table (include/core.h, after
	   package 4): until then m sets nothing and every mark is unset (E20) */
	case 'm':   /* nv_mark(): setmark_pos() knows these names */
		if (!checkclearop(cap->oap) && !has("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'`\"[]<>", cap->nchar))
			clearopbeep(cap->oap);
		break;
	case '\'': case '`': clearopbeep(cap->oap); break;
	}
}

/* ---- one normal command ---- */

static int pend[64], npend;
/* Neovim keeps a count as a number: while one is being typed, its keys
   start at pend[cnt_from] and add up to cnt */
static long cnt_from = -1, cnt;
static Cmdarg ca;
static int idx_valid;

/* normal_execute() with the keys in pend[]: 0 when more keys are needed */
static int run(void)
{
	int rd = 0, c;
#define GET(v) do { if (rd >= npend) return 0; v = pend[rd++]; } while (0)
	memset(&ca, 0, sizeof ca);
	ca.oap = &oa;
	ca.opcount = opcount;
	finish_op = oa.op_type != OP_NOP;
	if (!finish_op && !oa.regname) ca.opcount = 0;
	/* a Visual block operator can leave the cursor on the NUL ('virtualedit'
	   block was on for it); Neovim has it back on the last character before
	   the next command (measured: "j<C-v>D" on "ab\n\tef {", then any
	   command, even <Ignore>, finds column 6; nvim_win_get_cursor() right
	   after the D says 7) */
	check_cursor_col();
	update_curswant();
	long old_col = curswant();
	GET(c);
	/* normal_get_command_count(): CTRL-W takes a second count after it */
	int ctrl_w = 0;
	for (;;) {
		long from = rd - 1;
		while ((c >= '1' && c <= '9') || (ca.count0 != 0 && (c == '0' || c == KEY_DEL))) {
			if (c == KEY_DEL) ca.count0 /= 10;   /* <Del> drops the last digit */
			else if (ca.count0 > 99999999) ca.count0 = 999999999;
			else ca.count0 = ca.count0 * 10 + (c - '0');
			if (rd >= npend) { cnt_from = from; cnt = ca.count0; return 0; }
			c = pend[rd++];
		}
		if (c != 23 || ctrl_w || oa.op_type != OP_NOP) break;
		ctrl_w = 1;
		ca.opcount = ca.count0;
		ca.count0 = 0;
		GET(c);
	}
	if (ca.opcount != 0) {
		if (ca.count0) ca.count0 = ca.opcount >= 999999999 / ca.count0 ? 999999999 : ca.count0 * ca.opcount;
		else ca.count0 = ca.opcount;
	}
	ca.opcount = ca.count0;
	ca.count1 = ca.count0 ? ca.count0 : 1;
	if (ctrl_w) ca.nchar = c, c = 23;
	ca.cmdchar = c;
	idx_valid = known(c);
	int done = 0;
	if (!idx_valid) {
		clearopbeep(&oa);
		done = 1;
	} else {
		int fl = cmd_flags(c), pending_op = oa.op_type != OP_NOP;
		int need = (fl & NCH) && (((fl & NCH_NOP) == NCH_NOP && !pending_op) || (fl & NCH_ALW) == NCH_ALW
		                          || (c == 'q' && !pending_op && !reg_recording && !reg_executing)
		                          || ((c == 'a' || c == 'i') && (pending_op || VIsual_active)));
		if (need) {
			GET(ca.nchar);
			if (c == 'g' && (ca.nchar == 'r' || ca.nchar == '\'' || ca.nchar == '`')) GET(ca.extra_char);
		}
		if (ca.nchar == KEY_ESC || ca.extra_char == KEY_ESC) {
			clearop(&oa);
			done = 1;
		}
	}
	/* / and ?: getcmdline() is the command line mode, which gives K_PAT
	   when its line is entered and anything else when it is left */
	if (!done && (c == '/' || c == '?')) {
		if (rd >= npend) { cmdline(c, ""); return 0; }
		if (pend[rd++] != K_PAT) { clearop(&oa); done = 1; }
	}
	/* do_window() reads the key after CTRL-W g; none of its tab page and
	   tag commands is built, so each beeps */
	if (!done && c == 23 && (ca.nchar == 'g' || ca.nchar == 7)) GET(ca.extra_char);
#undef GET
	if (!done) execute(&ca);
	/* normal_finish_command() */
	if (!done) {
		if (!finish_op && !oa.op_type && !(cmd_flags(c) & KEEPREG)) clearop(&oa);
		do_pending_operator(&ca, old_col);
	}
	if (oa.op_type == OP_NOP) finish_op = 0;
	opcount = ca.opcount;
	/* mb_check_adjust_col(): ^ on a blank a combining mark is on lands
	   inside the character */
	if (E->mode == MODE_NORMAL && W->col) W->col = (size_t)head(ml_get((long)W->line), (long)W->col);
	/* normal_check() before the next command: validate_cursor() */
	if (stuff_empty() && E->mode == MODE_NORMAL) curs_columns();
	if (!(ca.retval & CA_COMMAND_BUSY)) restart_insert();
	return 1;
}

/* back to Insert mode after its CTRL-O command (normal_execute()); a
   command line ends that command when it is done */
void restart_insert(void)
{
	if (oa.op_type == OP_NOP && restart_edit && !VIsual_active && stuff_empty() && oa.regname == 0
	    && E->mode == MODE_NORMAL)
		edit_start(restart_edit, 1);
}

/* a command half typed: vgetorpeek() would be waiting for its next key */
int normal_pending(void) { return npend > 0; }
/* only a count and a register are typed: Neovim checks maps for the
   command's first key there too (safe_vgetc()).  A map that runs takes them,
   as its <Cmd> does (measured: with nnoremap <Space>ff <Cmd>echo<CR>,
   "2 ffx" on "abcb" deletes one character) */
int normal_mappable(int drop)
{
	if (oa.op_type != OP_NOP) return 0;
	for (int i = 0; i < npend; i++)
		if (pend[i] == '"') { if (++i == npend) return 0; }   /* the name is read unmapped */
		else if (pend[i] < '0' || pend[i] > '9') return 0;
	if (drop) { npend = 0; opcount = 0; clearop(&oa); }
	return 1;
}
void normal_reset(void) { search_reset(); sub_reset(); clearop(&oa); opcount = 0; VIsual_active = 0; redo_n = old_redo_n = 0; npend = 0; memset(&b_visual, 0, sizeof b_visual); }

void normal_key(int k)
{
	if (npend < (int)(sizeof pend / sizeof *pend)) pend[npend++] = k;
	cnt_from = -1;
	if (run()) npend = 0;
	else if (cnt_from >= 0 && npend - cnt_from > 9) {
		/* the count's digits replay to the same number (it is at most
		   999999999), so a held digit key never fills pend[] */
		char d[16];
		int n = cnt ? snprintf(d, sizeof d, "%ld", cnt) : 0;   /* 0 after <Del>: no digit */
		for (int i = 0; i < n; i++) pend[cnt_from + i] = d[i];
		npend = (int)cnt_from + n;
	}
}

/* ---- do_pending_operator() ---- */

static void get_op_vcol(Oparg *oap, long redo_vcol, int initial)
{
	long start, end;
	if (VIsual_mode != 22 || (!initial && oap->end.col < W->w)) return;
	oap->motion_type = MBLOCK;
	oap->block_mode = 1;
	if (oap->end.col < ml_len(oap->end.lnum)) oap->end.col = head(ml_get(oap->end.lnum), oap->end.col);
	getvvcol(oap->start, &oap->start_vcol, &oap->end_vcol);
	if (!redo_VIsual_busy) {
		getvvcol(oap->end, &start, &end);
		if (start < oap->start_vcol) oap->start_vcol = start;
		if (end > oap->end_vcol) oap->end_vcol = end;
	}
	if (curswant() == MAXCOL) {
		/* '$': the end of the longest line, the cell after its last
		   character (getvcol() of the NUL) */
		oap->end_vcol = 0;
		for (long l = oap->start.lnum; l <= oap->end.lnum; l++) {
			getvvcol((Pos){ l, ml_len(l), cur().coladd }, NULL, &end);
			if (end > oap->end_vcol) oap->end_vcol = end;
		}
	} else if (redo_VIsual_busy) oap->end_vcol = oap->start_vcol + redo_vcol - 1;
	/* the upper-left and lower-right corners of the block */
	W->line = (size_t)oap->end.lnum;
	coladvance(oap->end_vcol);
	oap->end = cur();
	setcur(oap->start);
	coladvance(oap->start_vcol);
	oap->start = cur();
}

static void do_pending_operator(Cmdarg *cap, long old_col)
{
	Oparg *oap = cap->oap;
	if (!((finish_op || VIsual_active) && oap->op_type != OP_NOP)) return;
	int include_line_break = 0;
	oap->is_visual = VIsual_active;
	oap->block_mode = 0;
	if (oap->motion_force == 'V') oap->motion_type = MLINE;
	else if (oap->motion_force == 'v') {
		if (oap->motion_type == MLINE) oap->inclusive = 0;
		else if (oap->motion_type == MCHAR) oap->inclusive = !oap->inclusive;
		oap->motion_type = MCHAR;
	} else if (oap->motion_force == 22) {
		if (!VIsual_active) { VIsual_active = 1; VIsual = oap->start; }
		VIsual_mode = 22;
	}
	if (oap->op_type != OP_YANK && oap->op_type != OP_COLON && (!VIsual_active || oap->motion_force)) {
		prep_redo(oap->regname, cap->count0, get_op_char(oap->op_type), get_extra_op_char(oap->op_type),
		          oap->motion_force, cap->cmdchar, cap->nchar);
		if (cap->searchbuf) {   /* the search's line, to type again */
			size_t n = strlen(cap->searchbuf);
			for (size_t i = 0; i < n; i += uni_len((const unsigned char *)cap->searchbuf, n, i))
				redo_append((int)uni_cp((const unsigned char *)cap->searchbuf, n, i));
			redo_append('\n');
		}
	}
	if (redo_VIsual_busy) {
		oap->start = cur();
		long l = (long)W->line + redo_VIsual.line_count - 1;
		W->line = (size_t)(l < nlines() ? l : nlines() - 1);
		VIsual_mode = redo_VIsual.mode;
		if (redo_VIsual.vcol == MAXCOL || VIsual_mode == 'v') {
			if (VIsual_mode == 'v') {
				if (redo_VIsual.line_count <= 1) {
					/* w_virtcol: a Tab's last cell in Normal mode */
					validate_virtcol();
					set_want((long)W->vc.v + redo_VIsual.vcol - 1);
				} else set_want(redo_VIsual.vcol);
			} else set_want(MAXCOL);
			coladvance(curswant());
		}
		cap->count0 = redo_VIsual.count;
		cap->count1 = cap->count0 ? cap->count0 : 1;
	} else if (VIsual_active) {
		vis_set(0, VIsual);
		vis_set(1, cur());
		b_visual.mode = VIsual_mode;
		if (VIsual_mode_orig) { b_visual.mode = VIsual_mode_orig; VIsual_mode_orig = 0; }
		update_curswant();
		b_visual.curswant = curswant();
		/* unadjust_for_sel() does nothing for 'selection' inclusive */
		oap->start = VIsual;
		if (VIsual_mode == 'V') oap->start.col = 0;
	}
	if (lt(oap->start, cur())) {
		oap->end = cur();
		setcur(oap->start);
	} else {
		oap->end = oap->start;
		oap->start = cur();
	}
	if (oap->end.lnum >= nlines()) oap->end.lnum = nlines() - 1;
	if (oap->end.col > ml_len(oap->end.lnum)) oap->end.col = ml_len(oap->end.lnum);
	oap->line_count = oap->end.lnum - oap->start.lnum + 1;
	/* before VIsual_active is reset: 'virtualedit' is "block" */
	virtual_op = VIsual_active && VIsual_mode == 22;
	if (VIsual_active || redo_VIsual_busy) {
		get_op_vcol(oap, redo_VIsual.vcol, 1);
		if (!redo_VIsual_busy) {
			resel_VIsual_mode = VIsual_mode;
			if (curswant() == MAXCOL) resel_VIsual_vcol = MAXCOL;
			else {
				if (VIsual_mode != 22) oap->end_vcol = vcol_end(oap->end.lnum, oap->end.col);
				if (VIsual_mode == 22 || oap->line_count <= 1) {
					if (VIsual_mode != 22) oap->start_vcol = vcol_of(oap->start.lnum, oap->start.col);
					resel_VIsual_vcol = oap->end_vcol - oap->start_vcol + 1;
				} else resel_VIsual_vcol = oap->end_vcol;
			}
			resel_VIsual_line_count = oap->line_count;
		}
		if (oap->op_type != OP_YANK && oap->op_type != OP_COLON && oap->motion_force == 0) {
			int nchar = oap->op_type == OP_REPLACE ? cap->nchar : 0;
			prep_redo(oap->regname, 0, 0, 'v', get_op_char(oap->op_type), get_extra_op_char(oap->op_type), nchar);
			if (!redo_VIsual_busy) {
				redo_VIsual.mode = resel_VIsual_mode;
				redo_VIsual.vcol = resel_VIsual_vcol;
				redo_VIsual.line_count = resel_VIsual_line_count;
				redo_VIsual.count = cap->count0;
				redo_VIsual.arg = cap->arg;
			}
		}
		if (oap->motion_force == 0 || oap->motion_type == MLINE) oap->inclusive = 1;
		if (VIsual_mode == 'V') oap->motion_type = MLINE;
		else if (VIsual_mode == 'v') {
			oap->motion_type = MCHAR;
			if (oap->end.col >= ml_len(oap->end.lnum) && (include_line_break || 1)) {
				oap->inclusive = 0;
				/* the line break is included */
				/* !op_on_lines() */
				if (oap->op_type != OP_LSHIFT && oap->op_type != OP_RSHIFT && oap->op_type != OP_JOIN
				    && oap->op_type != OP_JOIN_NS && oap->op_type != OP_FORMAT && oap->end.lnum + 1 < nlines()) {
					oap->end.lnum++;
					oap->end.col = 0;
					oap->line_count++;
				}
			}
		}
		redo_VIsual_busy = 0;
		VIsual_active = 0;
	}
	if (oap->inclusive && oap->end.col < ml_len(oap->end.lnum)) {
		int l = clen(ml_get(oap->end.lnum), ml_len(oap->end.lnum), oap->end.col);
		if (l > 1) oap->end.col += l - 1;
	}
	set_curswant = 1;
	oap->empty = oap->motion_type != MLINE
	             && (!oap->inclusive || (oap->op_type == OP_YANK && gchar(oap->end) == 0))
	             && eq(oap->start, oap->end);
	/* an exclusive motion that ends in column 0 ends on the line before
	   (":help exclusive") */
	if (oap->motion_type == MCHAR && !oap->inclusive && !(cap->retval & CA_NO_ADJ_OP_END)
	    && oap->end.col == 0 && !oap->is_visual && oap->line_count > 1) {
		oap->end_adjusted = 1;
		oap->line_count--;
		oap->end.lnum--;
		const char *s = ml_get((long)W->line);
		long i = 0;
		while (i < (long)W->col && (s[i] == ' ' || s[i] == '\t')) i++;
		if (i == (long)W->col) oap->motion_type = MLINE;    /* inindent(0) */
		else {
			oap->end.col = ml_len(oap->end.lnum);
			if (oap->end.col) {
				/* the last byte, as Neovim's --oap->end.col: inclusive then
				   takes the whole character, which a head would split */
				oap->end.col--;
				oap->inclusive = 1;
			}
		}
	} else oap->end_adjusted = 0;
	switch (oap->op_type) {
	case OP_LSHIFT: case OP_RSHIFT:
		op_shift(oap, oap->is_visual ? cap->count1 : 1);
		break;
	case OP_JOIN_NS: case OP_JOIN:
		if (oap->line_count < 2) oap->line_count = 2;
		if ((long)W->line + oap->line_count > nlines()) beep();
		else do_join(oap->line_count, oap->op_type == OP_JOIN);
		break;
	case OP_DELETE:
		VIsual_reselect = 0;
		op_delete(oap);
		break;
	case OP_YANK:
		op_yank(oap);
		check_cursor_col();
		break;
	case OP_CHANGE:
		VIsual_reselect = 0;
		if (op_change(oap)) cap->retval |= CA_COMMAND_BUSY;
		break;
	case OP_TILDE: case OP_UPPER: case OP_LOWER:
		op_tilde(oap);
		check_cursor_col();
		break;
	case OP_INSERT: case OP_APPEND:
		VIsual_reselect = 0;
		op_insert(oap, cap->count1);
		break;
	case OP_REPLACE:
		VIsual_reselect = 0;
		op_replace(oap, cap->nchar);
		break;
	case OP_FORMAT:
		op_format(oap);
		check_cursor_col();
		break;
	case OP_COLON:
		/* op_colon() stuffs the command line, so normal_check() updates
		   curswant before it runs */
		update_curswant();
		cmdline(':', "'<,'>");
		break;
	case OP_NR_ADD: case OP_NR_SUB:
		VIsual_active = 1;
		op_addsub(oap, cap->count1, redo_VIsual.arg);
		VIsual_active = 0;
		check_cursor_col();
		break;
	default:
		clearopbeep(oap);
	}
	/* 'nostartofline': back to the column of before the command */
	if (oap->motion_type == MLINE && !oap->end_adjusted
	    && (oap->op_type == OP_LSHIFT || oap->op_type == OP_RSHIFT || oap->op_type == OP_DELETE)) {
		/* coladvance(curswant = old_col), with w_set_curswant still set
		   by the operator: curswant becomes the column reached */
		W->want = old_col >= MAXCOL ? WANT_EOL : (size_t)old_col;
		coladvance(old_col);
	}
	/* an insert the operator started keeps it until it ends (ins_esc()) */
	if (E->mode != MODE_INSERT) virtual_op = -1;
	clearop(oap);
}
