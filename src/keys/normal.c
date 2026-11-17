/* Normal and Visual mode: Neovim 0.12's normal.c and do_pending_operator()
   (ops.c), with FEATURES.md's settings.  Keys arrive one at a time; a
   command waits in pend[] until it is complete, and then runs as Neovim's
   normal_cmd() would have with all its keys read. */
#include <string.h>
#include "k.h"

typedef struct {
	Oparg *oap;
	int cmdchar, nchar, extra_char;
	long opcount, count0, count1;
	int arg, retval;
} Cmdarg;
enum { CA_COMMAND_BUSY = 1, CA_NO_ADJ_OP_END = 2 };

int VIsual_active, VIsual_mode;
Pos VIsual;
int finish_op;
static Oparg oa;
static long opcount;
static int VIsual_reselect, VIsual_mode_orig;
/* what gv selects again (curbuf->b_visual) */
static struct { Pos start, end; int mode; long curswant; int set; } b_visual;
/* what 1v and a repeated Visual operator select (resel_VIsual_*, redo_VIsual) */
static int resel_VIsual_mode;
static long resel_VIsual_vcol, resel_VIsual_line_count;
static struct { int mode; long vcol, line_count, count; int arg; } redo_VIsual;
static int redo_VIsual_busy;

long visual_last_line(void) { return b_visual.start.lnum > b_visual.end.lnum ? b_visual.start.lnum : b_visual.end.lnum; }

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

static int redobuf[4096], redo_n;
static int redo_cancelled;
int redo_busy;

void redo_reset(void) { if (!redo_busy) { redo_n = 0; redo_cancelled = 0; } }
long redo_get(const int **k) { *k = redobuf; return redo_n; }
void redo_cancel(void) { redo_cancelled = 1; }

int redo_append(int k)
{
	if (redo_busy || redo_cancelled) return 0;
	if (redo_n < (int)(sizeof redobuf / sizeof *redobuf)) redobuf[redo_n++] = k;
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
static int start_redo(long count)
{
	if (!redo_n) return 0;
	int b[4096], n = 0, i = 0;
	int c = redobuf[i++];
	if (c == '"') {
		b[n++] = '"';
		c = redobuf[i++];
		if (c >= '1' && c < '9') c++;
		b[n++] = c;
		c = i < redo_n ? redobuf[i++] : 0;
	}
	if (c == 'v') {
		VIsual = cur();
		VIsual_active = 1;
		VIsual_reselect = 1;
		redo_VIsual_busy = 1;
		c = i < redo_n ? redobuf[i++] : 0;
	}
	if (count) {
		while (c >= '0' && c <= '9') c = i < redo_n ? redobuf[i++] : 0;
		char d[24];
		int k = 0;
		do d[k++] = (char)('0' + count % 10); while (count /= 10);
		while (k) b[n++] = d[--k];
	}
	b[n++] = c;
	stuff_redo(b, n);
	stuff_redo(redobuf + i, redo_n - i);
	return 1;
}

/* start_redo_ins(): an insert's count repeats what was typed, from the
   redo buffer after the command character */
void start_redo_ins(void)
{
	long i = 0;
	while (i < redo_n && !strchr("AaIiRrOo", redobuf[i] < 0x80 ? redobuf[i] : 1)) i++;
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
	b_visual.start = VIsual;
	b_visual.end = cur();
	b_visual.mode = VIsual_mode;
	update_curswant();
	b_visual.curswant = curswant();
	b_visual.set = 1;
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
	if (!b_visual.set || b_visual.start.lnum >= nlines()) { beep(); return; }
	Pos tpos;
	if (VIsual_active) {
		int i = VIsual_mode;
		VIsual_mode = b_visual.mode;
		b_visual.mode = i;
		update_curswant();
		long w = curswant();
		set_want(b_visual.curswant);
		b_visual.curswant = w;
		tpos = b_visual.end;
		b_visual.end = cur();
		setcur(b_visual.start);
		b_visual.start = VIsual;
	} else {
		VIsual_mode = b_visual.mode;
		set_want(b_visual.curswant);
		tpos = b_visual.end;
		setcur(b_visual.start);
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
		long l1 = vcol_of(old.lnum, old.col), r1 = vcol_end(old.lnum, old.col);
		long l2 = vcol_of(VIsual.lnum, VIsual.col), r2 = vcol_end(VIsual.lnum, VIsual.col);
		long left = l1 < l2 ? l1 : l2, right = r1 > r2 ? r1 : r2;
		W->line = (size_t)VIsual.lnum;
		coladvance(left);
		VIsual = cur();
		W->line = (size_t)old.lnum;
		set_want(right);
		coladvance(right);
		if ((long)W->col == old.col) {
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

static void nv_findpar(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->inclusive = 0;
	cap->oap->use_reg_one = 1;
	set_curswant = 1;
	if (!findpar(&cap->oap->inclusive, cap->arg, cap->count1, 0, 0)) clearopbeep(cap->oap);
}

static void nv_brace(Cmdarg *cap)
{
	cap->oap->motion_type = MCHAR;
	cap->oap->use_reg_one = 1;
	cap->oap->inclusive = 0;
	set_curswant = 1;
	if (!findsent(cap->arg, cap->count1)) { clearopbeep(cap->oap); return; }
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
				if (eq(p, old)) { clearopbeep(oap); return; }
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
	static const char *ar[] = { "dl", "dh", "d$", "c$", "cl", "cc", "y$" };
	static const char str[] = "xXDCsSY";
	if (!checkclearopq(cap->oap)) {
		if (cap->count0) stuff_num(cap->count0);
		stuff_str(ar[strchr(str, cap->cmdchar) - str], 2);
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
	if (cap->nchar >= KEY_UP) { clearopbeep(cap->oap); return; }
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
		b_visual.start = b_op_start;
		b_visual.end = b_op_end;
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
	/* nv_kundo() asks for curswant to be recomputed from the cached
	   virtual column, which stays valid if no step invalidated it and the
	   cursor ends where it started (check_cursor_moved()): curswant is
	   then the column in the text before the undo (measured, nvim 0.12,
	   "jxu" on "b  a\nc\tfoo" gives 7) */
	int redo = cap->cmdchar == KEY_CTRL_R;
	size_t l0 = W->line, c0 = W->col, w0 = W->want, v0;
	int inval = 0;
	core_set_want(E);
	v0 = W->want;
	W->want = w0;
	/* u_doit() stops at the first step that finds nothing */
	long n = cap->count1;
	for (int r; n-- && (r = core_undo(E, redo)) >= 0;) inval |= r;
	extern void buf_changed(void);
	buf_changed();
	if (!inval && W->line == l0 && W->col == c0) W->want = v0;
	else core_set_want(E);
	set_curswant = 0;
}

static void nv_dot(Cmdarg *cap)
{
	if (checkclearopq(cap->oap)) return;
	if (!start_redo(cap->count0)) clearopbeep(cap->oap);
}

static void nv_regname(Cmdarg *cap)
{
	if (checkclearop(cap->oap)) return;
	if (cap->nchar && valid_yank_reg(cap->nchar, 0)) {
		cap->oap->regname = cap->nchar;
		cap->opcount = cap->count0;
	} else clearopbeep(cap->oap);
}

extern int reg_recording, reg_executing, reg_recorded;
int  do_record(int c);
int  do_execreg(int regname);

static void nv_record(Cmdarg *cap)
{
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

static void nv_colon(Cmdarg *cap)
{
	if (VIsual_active || cap->oap->op_type != OP_NOP || cap->count0) {
		/* ":'<,'>" and ":.,.+N" need ranges, which come with the ex
		   commands; until then such a command line is refused */
		if (VIsual_active) end_visual_mode();
		clearop(cap->oap);
		beep();
		return;
	}
	E->mode = MODE_CMDLINE;
	E->cmdtype = ':';
	E->cmdlen = E->cmdpos = 0;
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
		}
	}
	return -1;
}

static int get_op_char(int op)
{
	static const int c[] = { 0, 'd', 'y', 'c', '<', '>', 'g', 'g', 'g', 'J', 'g', 'r', 'I', 'A', 1, 24 };
	return c[op];
}

static int get_extra_op_char(int op)
{
	switch (op) {
	case OP_TILDE: return '~';
	case OP_UPPER: return 'U';
	case OP_LOWER: return 'u';
	case OP_JOIN_NS: return 'J';
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
	case 'j': case KEY_DOWN:     /* 'nowrap': screen lines are text lines */
		oap->motion_type = MLINE;
		if (!cursor_down(cap->count1)) clearopbeep(oap);
		break;
	case 'k': case KEY_UP:
		oap->motion_type = MLINE;
		if (!cursor_up(cap->count1)) clearopbeep(oap);
		break;
	case 'J': nv_join(cap); break;
	case '_': {
		/* g_: the last non-blank, count - 1 lines down */
		oap->motion_type = MCHAR;
		oap->inclusive = 1;
		set_want(MAXCOL);
		if (!cursor_down(cap->count1 - 1)) { clearopbeep(oap); break; }
		const char *s = ml_get((long)W->line);
		long i = ml_len((long)W->line);
		while (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t')) i--;
		W->col = (size_t)(i > 0 ? head(s, i - 1) : 0);
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
	case 'g': cap->arg = 0; nv_goto(cap); break;
	case '~': case 'u': case 'U': nv_operator(cap); break;
	case 'p': case 'P': nv_put(cap, 0); break;
	default: clearopbeep(oap);
	}
}

/* ---- the command table ---- */

enum { NCH = 1, NCH_NOP = 2 | NCH, NCH_ALW = 4 | NCH, KEEPREG = 8 };

static int cmd_flags(int c)
{
	switch (c) {
	case '"': return NCH_NOP | KEEPREG;
	case '\'': case '`': case 'F': case 'T': case '[': case ']': case 'f': case 'g': case 't': return NCH_ALW;
	case '@': case 'm': case 'r': return NCH_NOP;
	case 'a': case 'i': case 'q': return NCH;
	case 'C': case 'D': case 'S': case 'X': case 'Y': case 's': case 'x': case '.': return KEEPREG;
	}
	return 0;
}

static int known(int c)
{
	return strchr(" \"$%()+,-.0:;<>@ABCDEFGIJOPQRSTUWXYV[]^_abcdefghijklopqrstuvwxy{|}~", c) || c == 1 || c == 3
	       || c == 8 || c == 10 || c == 13 || c == 14 || c == 16 || c == 22 || c == 24 || c == KEY_CTRL_R
	       || c == KEY_ESC || c == KEY_BS || c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT;
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
	case '<': case '>': case 'c': case 'd': case 'y': nv_operator(cap); break;
	case '@': nv_at(cap); break;
	case 'A': case 'I': case 'a': case 'i': nv_edit(cap); break;
	case 'B': cap->arg = 1; nv_bck_word(cap); break;
	case 'b': cap->arg = 0; nv_bck_word(cap); break;
	case 'C': case 'D': case 'X': case 'Y': case 'x': nv_abbrev(cap); break;
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
	}
}

/* ---- one normal command ---- */

static int pend[64], npend;
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
	update_curswant();
	long old_col = curswant();
	GET(c);
	while ((c >= '1' && c <= '9') || (ca.count0 != 0 && c == '0')) {
		if (ca.count0 > 99999999) ca.count0 = 999999999;
		else ca.count0 = ca.count0 * 10 + (c - '0');
		GET(c);
	}
	if (ca.opcount != 0) {
		if (ca.count0) ca.count0 = ca.opcount >= 999999999 / ca.count0 ? 999999999 : ca.count0 * ca.opcount;
		else ca.count0 = ca.opcount;
	}
	ca.opcount = ca.count0;
	ca.count1 = ca.count0 ? ca.count0 : 1;
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
#undef GET
	if (!done) execute(&ca);
	/* normal_finish_command() */
	if (!done) {
		if (!finish_op && !oa.op_type && !(cmd_flags(ca.cmdchar) & KEEPREG)) clearop(&oa);
		do_pending_operator(&ca, old_col);
	}
	if (oa.op_type == OP_NOP) finish_op = 0;
	opcount = ca.opcount;
	/* normal_check() before the next command; an insert the command
	   started keeps curswant as it is (edit() runs inside the command) */
	if (E->mode != MODE_INSERT) update_curswant();
	/* back to Insert mode after its CTRL-O command (normal_execute()) */
	if (oa.op_type == OP_NOP && restart_edit && !VIsual_active && !(ca.retval & CA_COMMAND_BUSY)
	    && stuff_empty() && oa.regname == 0 && E->mode == MODE_NORMAL)
		edit_start(restart_edit, 1);
	return 1;
}

void normal_key(int k)
{
	if (npend < (int)(sizeof pend / sizeof *pend)) pend[npend++] = k;
	if (run()) npend = 0;
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
	if (oap->op_type != OP_YANK && (!VIsual_active || oap->motion_force))
		prep_redo(oap->regname, cap->count0, get_op_char(oap->op_type), get_extra_op_char(oap->op_type),
		          oap->motion_force, cap->cmdchar, cap->nchar);
	if (redo_VIsual_busy) {
		oap->start = cur();
		long l = (long)W->line + redo_VIsual.line_count - 1;
		W->line = (size_t)(l < nlines() ? l : nlines() - 1);
		VIsual_mode = redo_VIsual.mode;
		if (redo_VIsual.vcol == MAXCOL || VIsual_mode == 'v') {
			if (VIsual_mode == 'v') {
				if (redo_VIsual.line_count <= 1) {
					set_want(vcol_of((long)W->line, (long)W->col) + redo_VIsual.vcol - 1);
				} else set_want(redo_VIsual.vcol);
			} else set_want(MAXCOL);
			coladvance(curswant());
		}
		cap->count0 = redo_VIsual.count;
		cap->count1 = cap->count0 ? cap->count0 : 1;
	} else if (VIsual_active) {
		b_visual.start = VIsual;
		b_visual.end = cur();
		b_visual.mode = VIsual_mode;
		if (VIsual_mode_orig) { b_visual.mode = VIsual_mode_orig; VIsual_mode_orig = 0; }
		update_curswant();
		b_visual.curswant = curswant();
		b_visual.set = 1;
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
		if (oap->op_type != OP_YANK && oap->motion_force == 0) {
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
				if (oap->op_type != OP_LSHIFT && oap->op_type != OP_RSHIFT && oap->op_type != OP_JOIN
				    && oap->op_type != OP_JOIN_NS && oap->end.lnum + 1 < nlines()) {
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
				oap->end.col = head(ml_get(oap->end.lnum), oap->end.col - 1);
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
