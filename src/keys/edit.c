/* Insert and Replace mode: Neovim 0.12's edit.c (insert_enter(),
   insert_handle_key(), insertchar(), ins_bs(), ins_tab(), ins_eol(),
   ins_esc(), stop_insert(), stop_arrow(), start_arrow()) and the parts of
   change.c it calls (ins_char_bytes(), open_line()), with FEATURES.md's
   settings: autoindent, expandtab, shiftwidth and softtabstop 4, smarttab,
   backspace=indent,eol,start.  Neovim 0.12's default mappings of CTRL-W and
   CTRL-U to "<C-G>u" before them are built in. */
#include <stdlib.h>
#include <string.h>
#include "k.h"
#include "uni.h"

enum { SW = 4 };
enum { BACKSPACE_CHAR = 1, BACKSPACE_WORD, BACKSPACE_WORD_NOT_SPACE, BACKSPACE_LINE };

int restart_edit, arrow_used, did_ai_flag;
int ins_at_eol;
static int cmdchar_s, did_restart_edit, update_Insstart_orig, ins_need_undo;
static int inserted_space, lastc, wait_for, batch_len, check_owed;
static long count_s, ai_col, new_insert_skip, o_lnum;
static Pos Insstart, Insstart_orig;

/* the keys of the last insert (Neovim's last_insert: the redo buffer at its
   end), and how many of them are the command that started it */
static int *last_ins;
static long last_ins_n, last_ins_skip;

void edit_reset(void) { free(last_ins); last_ins = NULL; last_ins_n = last_ins_skip = 0; check_owed = 0; }

static int replacing(void) { return E->ks.replace; }

/* ---- the replace stack: what Replace mode overtyped, for BS ---- */

static char *rs;
static long rs_n, rs_cap;

static void replace_push(const char *s, long n)
{
	if (rs_n + n > rs_cap) {
		long c = (rs_n + n) * 2 + 16;
		char *t = realloc(rs, (size_t)c);
		if (!t) return;
		rs = t;
		rs_cap = c;
	}
	memcpy(rs + rs_n, s, (size_t)n);
	rs_n += n;
}

static void replace_push_nul(void) { replace_push("", 1); }

/* replace_pop_if_nul(): -1 empty, else the top byte, popped if NUL */
static int replace_pop_if_nul(void)
{
	if (!rs_n) return -1;
	int c = (unsigned char)rs[rs_n - 1];
	if (c == 0) rs_n--;
	return c;
}

/* replace_join(): remove the off'th NUL from the top */
static void replace_join(int off)
{
	for (long i = rs_n; --i >= 0;)
		if (rs[i] == 0 && off-- <= 0) {
			memmove(rs + i, rs + i + 1, (size_t)(rs_n - i - 1));
			rs_n--;
			return;
		}
}

/* ins_str(): past the text with 'virtualedit' the gap fills with spaces */
static void ins_bytes(const char *s, long n)
{
	if (virtual_active() && cur().coladd > 0) coladvance_force(getviscol());
	replace_bytes((long)W->line, (long)W->col, 0, s, n);
	W->col += (size_t)n;
}

/* mb_replace_pop_ins(): insert the character on top; its utf_head_off()
   stops at the entry's NUL and steps by cluster, so a ZWJ emoji comes back
   whole, not one code point before the cursor at a time */
static void mb_replace_pop_ins(void)
{
	long e = rs_n - 1;
	while (e > 0 && rs[e - 1]) e--;
	long i = e + (long)uni_gprev((const unsigned char *)rs + e, (size_t)(rs_n - e), (size_t)(rs_n - e));
	long n = rs_n - i;
	rs_n = i;
	ins_bytes(rs + i, n);
}

static void replace_pop_ins(void)
{
	while (replace_pop_if_nul() > 0) {
		mb_replace_pop_ins();
		dec_cursor();
	}
}

static int del_char(int fixpos)
{
	long l = (long)W->line, n = ml_len(l), c = (long)W->col;
	if (c >= n) return 0;
	long k = clen(ml_get(l), n, c);
	/* del_bytes(): taking off the last character backs up, before the edit */
	if (fixpos && c > 0 && c + k >= n && !restart_edit) W->col = (size_t)head(ml_get(l), c - 1);
	replace_bytes(l, c, k, "", 0);
	return 1;
}

static void replace_do_bs(void)
{
	int cc = replace_pop_if_nul();
	if (cc > 0) {
		del_char(0);
		replace_pop_ins();
	} else if (cc == 0) del_char(0);
}

/* ---- inserting ---- */

void ins_char_bytes(const char *s, long n)
{
	if (virtual_active() && cur().coladd > 0) coladvance_force(getviscol());   /* break tabs */
	long l = (long)W->line, c = (long)W->col, len = ml_len(l), old = 0;
	if (replacing()) {
		if (c < len) old = clen(ml_get(l), len, c);
		replace_push_nul();
		replace_push(ml_get(l) + c, old);
	}
	replace_bytes(l, c, old, s, n);
	W->col += (size_t)n;
}

static void ins_char(int c)
{
	char b[4];
	ins_char_bytes(b, uni_put((unsigned char *)b, (unsigned)c));
}

static int inindent(void)
{
	const char *s = ml_get((long)W->line);
	long i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	return i >= (long)W->col;
}

/* stop_arrow(): before a change; after a cursor key a new insert starts */
static void stop_arrow(void)
{
	if (arrow_used) {
		Insstart = cur();
		if (Insstart.col > Insstart_orig.col && !ins_need_undo) update_Insstart_orig = 0;
		u_save_cursor();
		arrow_used = 0;
		ins_need_undo = 0;
		ai_col = 0;
		redo_reset();
		redo_add_str("1i");      /* pretend an insert started */
		new_insert_skip = 2;
	} else if (ins_need_undo) {
		u_save_cursor();
		ins_need_undo = 0;
	}
}

static void stop_insert(Pos end, int esc, int nomove);

/* start_arrow(): a cursor key ends the insert, as Esc would for undo and redo */
static void start_arrow(Pos end)
{
	if (!arrow_used) {
		redo_append(KEY_ESC);
		stop_insert(end, 0, 0);
		arrow_used = 1;
	}
}

static void stop_insert(Pos end, int esc, int nomove)
{
	redo_busy = 0;            /* stop_redo_ins() */
	rs_n = 0;
	const int *k;
	long n = redo_get(&k), added = n - new_insert_skip;
	if (!did_restart_edit || added > 0) {
		free(last_ins);
		last_ins = malloc(sizeof(int) * (size_t)(n + 1));
		if (last_ins) memcpy(last_ins, k, sizeof(int) * (size_t)n);
		last_ins_n = last_ins ? n : 0;
		last_ins_skip = added < 0 ? 0 : new_insert_skip;
	}
	/* after an unused autoindent, its blanks go and the cursor comes back */
	if (!arrow_used && !nomove && did_ai_flag && (esc || (long)W->line != end.lnum) && end.lnum < nlines()) {
		Pos tpos = cur();
		long prev_col = end.col;
		int cc;
		setcur(end);
		check_cursor_col();
		for (;;) {
			if (gchar_cursor() == 0 && W->col > 0) W->col--;
			cc = gchar_cursor();
			if (cc != ' ' && cc != '\t') break;
			if (!del_char(1)) break;
		}
		if ((long)W->line != tpos.lnum) setcur(tpos);
		else if ((long)W->col < prev_col) {
			Pos t = cur();
			t.col++;
			if (cc != 0 && gchar(t) == 0) W->col++;
		}
	}
	did_ai_flag = 0;
	b_op_start = Insstart;
	b_op_start_orig = Insstart_orig;
	b_op_end = end;
}

/* insertchar(): one typed character */
static void insertchar(int c, int pending, int ctrlv)
{
	did_ai_flag = 0;
	ins_char(c);
	if (ctrlv && c >= '0' && c <= '9') {
		/* redo_literal(): after CTRL-V a digit is read as a code */
		redo_append('0');               /* "%03d" of 48 to 57 */
		redo_append('0' + c / 10);
		redo_append('0' + c % 10);
	} else redo_append(c);
	if (ctrlv) { batch_len = 0; return; }
	/* typeahead of one-byte characters goes in as one string, with no
	   further keys handled (inserted_space stays) */
	int special = c < ' ' || c >= 0x7f || c == '0' || c == '^';
	batch_len = !special && pending && !replacing() ? batch_len + 1 : 0;
	if (batch_len >= 100) batch_len = 0;   /* INPUT_BUFLEN */
}

/* open_line(dir) with 'autoindent', from Insert mode (Enter) or Normal mode
   (o and O, dir BACKWARD for O) */
/* the indent before "^ CTRL-D", which the next line gets back (Neovim's
   old_indent, open_line()'s second_line_indent) */
long old_indent;

static int open_line(int dir)
{
	long l = (long)W->line, c = (long)W->col, len = ml_len(l);
	const char *s = ml_get(l);
	int insert = E->mode == MODE_INSERT, trunc = dir == FORWARD && did_ai_flag;
	/* the indent of the text before the cursor (p_extra cut it off), byte
	   by byte as get_indent_str() counts it */
	long ind = 0, rest = len;
	for (long i = 0; (!insert || i < c) && (s[i] == ' ' || s[i] == '\t'); i++) ind += s[i] == '\t' ? 8 - ind % 8 : 1;
	if (!ind && insert) ind = old_indent;
	ai_col = 0;
	char *line = malloc((size_t)len + 1);
	if (!line) return 0;
	memcpy(line, s, (size_t)len + 1);
	long extra = len;          /* p_extra: the text that moves to the new line */
	if (insert) {
		extra = c;
		if (replacing()) replace_push_nul();
		while (extra < len && whitechar(line, len, extra)) {
			if (replacing()) replace_push(line + extra, 1);
			extra++;
		}
	}
	long nl_len = ind + (insert ? len - extra : 0);
	char *nl = malloc((size_t)nl_len + 1);
	if (!nl) { free(line); return 0; }
	memset(nl, ' ', (size_t)ind);
	if (insert) memcpy(nl + ind, line + extra, (size_t)(len - extra));
	if (dir == BACKWARD) {
		nl[nl_len] = '\n';
		replace_bytes(l, 0, 0, nl, nl_len + 1);
	} else {
		if (trunc || insert) {
			rest = insert ? c : len;
			if (trunc) while (rest > 0 && (line[rest - 1] == ' ' || line[rest - 1] == '\t')) rest--;
		}
		/* one edit: the end of this line becomes the new line; when it
		   keeps all its text the new line goes in after its break, so the
		   core sees a line appended below it, also when it is empty
		   (appended_lines_mark(): "x" V d o Esc gv stays on line 1) */
		char *b = malloc((size_t)nl_len + 2);
		if (b) {
			if (rest == len) {
				memcpy(b, nl, (size_t)nl_len);
				b[nl_len] = '\n';
				core_edit(E, core_line_start(E, (size_t)l + 1), 0, b, (size_t)nl_len + 1);
				buf_changed();
			} else if (rest || ind) {
				/* only the white space after the break goes, and the
				   break and indent come in: what follows keeps its marks,
				   as Neovim's open_line() splices extmarks (a snippet's
				   stops; package 5's, agreed) */
				b[0] = '\n';
				memcpy(b + 1, nl, (size_t)ind);
				replace_bytes(l, rest, extra - rest, b, ind + 1);
			} else {
				/* a bare break at column 0 would be a line put above, which
				   moves this line's marks down: its text goes and comes
				   back as a line appended below (ml_append() after this
				   line: m' on "  " then r<CR> `` stays on line 1) */
				replace_bytes(l, 0, len, "", 0);
				append_lines(l, nl, nl_len);
			}
			free(b);
		}
		l++;
	}
	free(line);
	free(nl);
	setcur((Pos){ l, ind, 0 });
	ai_col = ind;
	if (replacing()) for (long i = 0; i < ind; i++) replace_push_nul();
	did_ai_flag = 1;
	return 1;
}

/* o and O (n_opencmd()) */
int open_cmd(int dir)
{
	u_save(W->line + (dir == FORWARD), W->line + (dir == FORWARD));   /* n_opencmd()'s u_save() */
	return open_line(dir);
}

static void ins_eol(void)
{
	stop_arrow();
	if (replacing()) replace_push_nul();
	/* on the NUL if coladd is past the last character (after CTRL-O) */
	if (virtual_active() && cur().coladd > 0) coladvance(getviscol());
	redo_append('\n');
	open_line(FORWARD);
	old_indent = 0;
}

static int ins_tab(void)
{
	stop_arrow();
	did_ai_flag = 0;
	redo_append('\t');
	/* smarttab in the indent uses 'shiftwidth', elsewhere 'softtabstop':
	   both 4 */
	long v = getviscol(), temp = SW - v % SW;   /* get_nolist_virtcol(): with coladd */
	ins_char(' ');
	while (--temp > 0) {
		ins_bytes(" ", 1);
		if (replacing()) replace_push_nul();
	}
	return 0;
}

static int ins_bs(int c, int mode)
{
	if (is_empty_buf() || (W->line == 0 && W->col == 0)) return 0;
	stop_arrow();
	int in_indent = inindent(), did = 0;
	/* 'virtualedit': BS eats a virtual space, CTRL-W all of them, CTRL-U
	   all of them and goes on */
	Pos p = cur();
	if (p.coladd > 0) {
		if (mode == BACKSPACE_CHAR) { p.coladd--; setcur(p); return 1; }
		p.coladd = 0;
		setcur(p);
		if (mode == BACKSPACE_WORD) return 1;
	}
	if (W->col == 0) {
		long lnum = Insstart.lnum;
		if ((long)W->line == lnum) {
			Insstart.lnum--;
			Insstart.col = ml_len(Insstart.lnum);
		}
		int cc = -1;
		if (replacing()) cc = replace_pop_if_nul();
		if (replacing() && (long)W->line <= lnum) dec_cursor();
		else {
			int temp = gchar_cursor();
			W->line--;
			do_join(2, 0);
			if (temp == 0 && gchar_cursor() != 0) inc_cursor();
			if (replacing()) {
				while (cc > 0) {
					size_t save = W->col;
					mb_replace_pop_ins();
					W->col = save;
					cc = replace_pop_if_nul();
				}
				replace_pop_ins();
			}
		}
		did_ai_flag = 0;
	} else {
		long mincol = 0;
		if (mode == BACKSPACE_LINE) {   /* 'autoindent': keep the indent */
			size_t save = W->col;
			beginline(BL_WHITE);
			if (W->col < save) mincol = (long)W->col;
			W->col = save;
		}
		const char *s = ml_get((long)W->line);
		long col = (long)W->col;
		if (mode == BACKSPACE_CHAR
		    && (in_indent || s[col - 1] == '\t' || (s[col - 1] == ' ' && (!inserted_space || arrow_used)))) {
			/* back to the previous multiple of 'shiftwidth' (smarttab, in
			   the indent) or 'softtabstop' (both 4) */
			inserted_space = 0;
			long l = (long)W->line, vcol = 0, space_vcol = 0, space_col = 0, i = 0;
			int prev_space = 0;
			while (i < col) {
				int cur_space = s[i] == ' ' || s[i] == '\t';
				if (!prev_space && cur_space) { space_col = i; space_vcol = vcol; }
				long k = clen(s, ml_len(l), i);
				vcol = vcol_of(l, i + k);
				i += k;
				prev_space = cur_space;
			}
			long want = vcol > 0 ? vcol - 1 : 0;
			want -= want % SW;
			for (;;) {
				long k = clen(s, ml_len(l), space_col);
				long size = vcol_of(l, space_col + k) - vcol_of(l, space_col);
				if (space_vcol + size > want) break;
				space_vcol += size;
				space_col += k;
			}
			while ((long)W->col > space_col) {
				dec_cursor();
				if (replacing()) {
					if ((long)W->line != Insstart.lnum || (long)W->col >= Insstart.col) replace_do_bs();
				} else del_char(0);
			}
			for (; space_vcol < want; space_vcol++) {
				if ((long)W->line == Insstart_orig.lnum && (long)W->col < Insstart_orig.col) Insstart_orig.col = (long)W->col;
				ins_bytes(" ", 1);
				if (replacing()) replace_push_nul();
			}
		} else {
			int cclass = cls_of(gchar_cursor()), temp = 0;
			do {
				dec_cursor();
				int cc = gchar_cursor(), prev = cclass;
				cclass = cls_of(cc);
				int space = cc == ' ' || (cc >= 9 && cc <= 13);
				if (mode == BACKSPACE_WORD && !space) {
					mode = BACKSPACE_WORD_NOT_SPACE;
					temp = cls_of(cc) >= 2;
				} else if (mode == BACKSPACE_WORD_NOT_SPACE && (space || (cls_of(cc) >= 2) != temp || prev != cclass)) {
					inc_cursor();
					break;
				}
				if (replacing()) replace_do_bs();
				else del_char(0);
				if (mode == BACKSPACE_CHAR) break;
			} while ((long)W->col > mincol && ((long)W->line != Insstart_orig.lnum || (long)W->col != Insstart_orig.col));
		}
		did = 1;
	}
	if (W->col <= 1) did_ai_flag = 0;
	redo_append(c);
	if ((long)W->line == Insstart_orig.lnum && (long)W->col < Insstart_orig.col) Insstart_orig.col = (long)W->col;
	return did;
}

/* ins_del(): the character under the cursor, or the line break after it
   ('backspace' eol) joining without a space */
static void ins_del(void)
{
	stop_arrow();
	if (gchar_cursor() == 0) {
		size_t col = W->col;
		/* on the last line the join fails; stop_arrow() has made the undo
		   step already, so redo is lost as in Neovim */
		if ((long)W->line + 1 < nlines()) { do_join(2, 0); W->col = col; }
	} else del_char(0);
	did_ai_flag = 0;
	redo_append(KEY_DEL);
}

/* CTRL-T and CTRL-D: change_indent() by one 'shiftwidth', rounded */
static void ins_shift(int c)
{
	stop_arrow();
	redo_append(c);
	long l = (long)W->line;
	if (c == 4 && (lastc == '0' || lastc == '^') && W->col > 0) {
		W->col--;
		del_char(0);
		if (replacing()) replace_pop_ins();
	}
	long vcol = vcol_of(l, (long)W->col), start_col = (long)W->col;
	long new_cursor_col = (long)W->col;
	beginline(BL_WHITE);
	new_cursor_col -= (long)W->col;
	long insstart_less = (long)W->col;
	if (new_cursor_col < 0) vcol = get_indent(l) - vcol;
	if (new_cursor_col > 0) start_col = -1;
	long ind = get_indent(l), n;
	if (c == 4 && lastc == '^') old_indent = ind;
	if (c == 4 && (lastc == '0' || lastc == '^')) n = 0;
	else if (c == 4) n = ind % SW ? ind - ind % SW : (ind >= SW ? ind - SW : 0);
	else n = ind - ind % SW + SW;
	set_indent(l, n);
	beginline(BL_WHITE);
	insstart_less -= (long)W->col;
	if (new_cursor_col >= 0) {
		if (new_cursor_col == 0) insstart_less = MAXCOL;
		new_cursor_col += (long)W->col;
	} else {
		/* the cursor was in the indent: keep its distance from the text */
		vcol = get_indent(l) - vcol;
		long end_vcol = vcol < 0 ? 0 : vcol;
		const char *s = ml_get(l);
		long len = ml_len(l), i = 0;
		vcol = 0;
		if (len) {
			for (;;) {
				long k = clen(s, len, i), next = vcol_of(l, i + k);
				if (next > end_vcol) break;
				vcol = next;
				i += k;
				if (i >= len) break;
			}
		}
		new_cursor_col = i;
		if (vcol != end_vcol) {
			W->col = (size_t)i;
			for (long k = vcol; k < end_vcol; k++) ins_bytes(" ", 1);
			new_cursor_col += end_vcol - vcol;
		}
		insstart_less = MAXCOL;
	}
	W->col = (size_t)(new_cursor_col < 0 ? 0 : new_cursor_col);
	set_curswant = 1;
	if ((long)W->line == Insstart.lnum && Insstart.col != 0)
		Insstart.col = Insstart.col <= insstart_less ? 0 : Insstart.col - insstart_less;
	ai_col = ai_col <= insstart_less ? 0 : ai_col - insstart_less;
	if (replacing() && start_col >= 0) {
		for (; start_col > (long)W->col; start_col--) replace_join(0);
		for (; start_col < (long)W->col; start_col++) replace_push_nul();
	}
	if (did_ai_flag && !linewhite(l)) did_ai_flag = 0;
}

/* ---- starting and ending ---- */

static int restart_save, restore_restart;

/* invoke_edit(): "a<C-O>a" does not nest Insert mode, but "a<C-O>." and
   "a<C-O>rx" go back to it once their insert ends */
void invoke_edit(int cmdchar, long count, int repl)
{
	restart_save = repl || !stuff_empty() ? restart_edit : 0;
	restart_edit = 0;
	edit_start(cmdchar, count);
	restore_restart = 1;
}

/* edit(cmdchar, startln, count): cmdchar 0 for an operator (c, block I
   and A), where the redo buffer is the operator's */
void edit_start(int cmdchar, long count)
{
	restore_restart = 0;
	old_indent = 0;
	int startln = cmdchar == 'o' || cmdchar == 'O';
	did_restart_edit = restart_edit;
	update_Insstart_orig = 1;
	Insstart = cur();
	if (startln) Insstart.col = 0;
	if (!did_ai_flag) ai_col = 0;
	if (cmdchar && !restart_edit) {
		redo_reset();
		redo_num_append(count);
		redo_append(cmdchar);
		if (cmdchar == 'g') redo_append('I');
		else if (cmdchar == 'r') count = 1;
	}
	cmdchar_s = cmdchar;
	count_s = count;
	E->mode = MODE_INSERT;
	E->ks.replace = cmdchar == 'R';
	if (restart_edit && stuff_empty()) {
		arrow_used = 1;
		restart_edit = 0;
		/* after CTRL-O at the end of line, back after it */
		update_curswant();
		long l = (long)W->line, len = ml_len(l), c = (long)W->col;
		long v = W->want == WANT_EOL ? MAXCOL : (long)W->want;
		if (((ins_at_eol && l == o_lnum) || v > vcol_of(l, c)) && c < len) {
			long k = clen(ml_get(l), len, c);
			if (c + k >= len) W->col += (size_t)k;
		}
		ins_at_eol = 0;
	} else arrow_used = 0;
	ins_need_undo = 1;
	if (!did_restart_edit) fold_open_cursor();   /* edit(): the cursor line is not in a closed fold */
	const int *k;
	new_insert_skip = redo_get(&k);
	inserted_space = 0;
	batch_len = 0;
	wait_for = 0;
	lastc = 0;
	insert_check();
}

/* insert_check(): before each key */
void insert_check(void)
{
	if (arrow_used) count_s = 0;
	if (update_Insstart_orig) Insstart_orig = Insstart;
	if (!arrow_used) set_curswant = 1;
	if (count_s <= 1) update_curswant();
}

extern void change_done(void);
extern void insert_block_done(Pos insstart);

/* ins_esc(): 1 when Insert mode ends */
static int ins_esc(int nomove)
{
	long temp = (long)W->col;
	if (!arrow_used) {
		if (cmdchar_s != 'r') redo_append(KEY_ESC);
		if (--count_s > 0) {   /* repeat what was typed */
			start_redo_ins();
			if (cmdchar_s == 'r') { int esc = KEY_ESC; stuff_redo(&esc, 1); }
			return 0;
		}
		stop_insert(cur(), 1, nomove);
	}
	/* when an autoindent was removed, curswant stays after the indent */
	if (!restart_edit && temp == (long)W->col) set_curswant = 1;
	B->mark[MARK_CARET] = (Mark){ W->line, W->col, 1 };   /* the '^ mark */
	if (!nomove && (W->col != 0 || cur().coladd > 0) && (!restart_edit || (gchar_cursor() == 0 && !VIsual_active))) {
		if (cur().coladd > 0) {
			oneleft();
			if (restart_edit) { Pos p = cur(); p.coladd++; setcur(p); }
		} else {
			W->col--;
			W->col = (size_t)head(ml_get((long)W->line), (long)W->col);
		}
	}
	E->mode = MODE_NORMAL;
	E->ks.replace = 0;
	if (restore_restart && !restart_edit) restart_edit = restart_save;
	restore_restart = 0;
	insert_block_done(Insstart);
	change_done();
	virtual_op = -1;          /* op_change() and op_insert() return */
	return 1;
}

/* CTRL-G u: a new undoable change starts */
static void ctrl_g_u(void)
{
	undo_close(B->undo);
	ins_need_undo = 1;
	update_Insstart_orig = 0;
	Insstart = cur();
}


/* insert_special(): a character after CTRL-V goes in as it is; a special
   key as its name */
static void insert_special(int c)
{
	static const char *const name[] = { "<Up>", "<Down>", "<Left>", "<Right>" };
	if (c >= KEY_UP) {
		if (c > KEY_RIGHT && c != KEY_DEL) return;
		const char *p = c == KEY_DEL ? "<Del>" : name[c - KEY_UP];
		long n = (long)strlen(p) - 1;
		stop_arrow();
		ins_bytes(p, n);
		for (long i = 0; i < n; i++) redo_append(p[i]);
		insertchar('>', 0, 0);
		return;
	}
	stop_arrow();
	insertchar(c, 0, 1);
}

/* get_literal(), one key at a time: CTRL-V then a character, or a decimal
   (3 digits), octal (o, 3), hex (x, 2) or Unicode (u 4, U 8) code; the key
   that ends a short code is handled after it (vungetc()) */
static struct { int on, cc, i, hex, octal, unicode; } lit;
static int reg_literally;   /* CTRL-R CTRL-R, CTRL-O or CTRL-P */

static int literal_key(int nc, int pending)
{
	int brk = 0;
	if (nc == 'x' || nc == 'X') lit.hex = 1;
	else if (nc == 'o' || nc == 'O') lit.octal = 1;
	else if (nc == 'u' || nc == 'U') lit.unicode = nc;
	else {
		int d = nc >= '0' && nc <= '9' ? nc - '0' : nc >= 'a' && nc <= 'f' ? nc - 'a' + 10 : nc >= 'A' && nc <= 'F' ? nc - 'A' + 10 : -1;
		if (lit.hex || lit.unicode) { if (d < 0) brk = 1; else if (lit.cc <= 0x10FFFF) lit.cc = lit.cc * 16 + d; }
		else if (lit.octal) { if (d < 0 || d > 7) brk = 1; else lit.cc = lit.cc * 8 + d; }
		else { if (d < 0 || d > 9) brk = 1; else lit.cc = lit.cc * 10 + d; }
		if (!brk) lit.i++;
	}
	if (!brk) {
		if (lit.cc > 255 && !lit.unicode) lit.cc = 255;
		nc = 0;
		if (lit.hex ? lit.i < 2 : lit.unicode ? lit.i < (lit.unicode == 'u' ? 4 : 8) : lit.i < 3) return 1;
	}
	int cc = lit.cc;
	if (lit.i == 0) { cc = nc; nc = 0; }
	lit.on = 0;
	/* NUL is stored as NL, which this buffer cannot hold in a line */
	/* a code typed past U+10FFFF: Neovim writes bytes that are not UTF-8
	   (README); a special key (lit.i 0) inserts its name */
	if (cc != 0 && cc != '\n' && (lit.i == 0 || cc <= 0x10FFFF)) insert_special(cc);
	if (E->mode == MODE_INSERT) insert_check();
	if (nc) insert_key(nc, pending);
	return 1;
}

void insert_key(int k, int pending)
{
	int c = k;
	if (lit.on) { literal_key(c, pending); return; }
	if (wait_for) {
		int w = wait_for;
		wait_for = 0;
		if (w == 18 && !reg_literally && (c == 18 || c == 15 || c == 16)) {
			reg_literally = c;     /* a third key names the register */
			wait_for = 18;
			return;
		}
		if (w == 18) {        /* CTRL-R {register} */
			int lit = reg_literally;
			reg_literally = 0;
			if (!insert_reg(c, lit)) { /* vim_beep(): no flush */ }
			inserted_space = 0;
		} else if (w == 7) {  /* CTRL-G */
			if (c == 'u') ctrl_g_u();
		}
		c = w;   /* lastc is the command: ins_reg() reads its register itself */
		goto post;
	}
	/* inside insertchar()'s string: no key handling, and Neovim's edit()
	   runs insert_check() once for the string, not per character, which
	   keeps a long line from costing its length per key */
	if (batch_len && c >= ' ' && c < 0x7f && c != '0' && c != '^') {
		insertchar(c, pending, 0);
		check_owed = 1;
		return;
	}
	if (check_owed) { check_owed = 0; insert_check(); }
	batch_len = 0;
	switch (c) {
	case KEY_ESC: case KEY_CTRL_C:
		/* when the count repeats the insert, its CTRL-D sees the Esc
		   before it, not a typed 0 or ^ ("2a C-d ^ Esc" gives ^^) */
		lastc = c;
		ins_esc(0);
		return;
	case 15:                  /* CTRL-O: one Normal mode command */
		restart_edit = replacing() ? 'R' : 'I';
		ins_at_eol = !virtual_active() && gchar_cursor() == 0;   /* ins_ctrl_o(): with virtualedit the column stays */
		count_s = 0;
		ins_esc(0);
		if (ins_at_eol) o_lnum = (long)W->line;
		return;
	case 0: case 1:           /* CTRL-@ and CTRL-A: the last insert again */
		if (!stuff_inserted(0, 1, c == 1) && c != 1) { ins_esc(0); return; }
		inserted_space = 0;
		break;
	case 18: case 7: wait_for = c; return;
	case 22: case 17:         /* CTRL-V and CTRL-Q: the next key literally */
		redo_append(22);
		memset(&lit, 0, sizeof lit);
		lit.on = 1;
		return;
	case 4: case 20: ins_shift(c); inserted_space = 0; break;
	case KEY_BS: case 8: ins_bs(c, BACKSPACE_CHAR); break;
	case KEY_DEL: ins_del(); break;
	/* 0.12's default mappings put CTRL-G u before CTRL-W and CTRL-U; keys
	   read from the stuff buffers (redo, a repeated insert) are not mapped
	   (vgetorpeek() returns read_readbuffers() before mapping) */
	case 23: if (!key_stuffed) ctrl_g_u(); ins_bs(c, BACKSPACE_WORD); break;
	case 21: if (!key_stuffed) ctrl_g_u(); ins_bs(c, BACKSPACE_LINE); inserted_space = 0; break;
	case KEY_LEFT: {
		Pos t = cur();
		if (oneleft()) start_arrow(t);
		break;
	}
	case KEY_RIGHT:
		if (gchar_cursor() != 0 || virtual_active()) {
			start_arrow(cur());
			set_curswant = 1;
			if (virtual_active()) oneright();
			else W->col += (size_t)clen(ml_get((long)W->line), ml_len((long)W->line), (long)W->col);
		}
		break;
	case KEY_UP: case KEY_DOWN: {
		Pos t = cur();
		if (c == KEY_UP ? cursor_up(1) : cursor_down(1)) start_arrow(t);
		break;
	}
	case KEY_S_TAB:   /* an unmapped <S-Tab> is a Tab (insert_handle_key's K_S_TAB) */
	case KEY_TAB: inserted_space = 0; ins_tab(); break;
	case 10: case 13: ins_eol(); inserted_space = 0; break;
	default:
		/* insert_handle_key() goes to normalchar for CTRL-B, F, L, Z, backslash and _
		   outside CTRL-X mode; the other control keys are commands here or
		   not built (completion, digraphs) */
		if (c < ' ' && !has("\x02\x06\x0c\x1a\x1c\x1f", c)) break;
		if (c >= KEY_UP) break;          /* the mouse and the wheel do nothing here */
		if (c == ' ') inserted_space = 1;
		stop_arrow();
		insertchar(c, pending, 0);
	}
post:
	if (arrow_used) inserted_space = 0;
	lastc = c;
	if (E->mode == MODE_INSERT) insert_check();
}

/* ---- the last insert: CTRL-A, ". and "." with a register ---- */

/* stuff_inserted(): the last insert as typed keys, count times, after
   command character c (0: none), with an Esc unless no_esc */
int stuff_inserted(int c, long count, int no_esc)
{
	if (!last_ins) { core_msg(E, "E29: No inserted text yet"); return 0; }
	const int *k = last_ins + last_ins_skip;
	long n = last_ins_n - last_ins_skip;
	for (long i = n; i-- > 0;)
		if (k[i] == KEY_ESC) { n = i; break; }
	/* a trailing 0 or ^ would make a later CTRL-D special: Neovim quotes it,
	   as <C-V>048 and <C-V>^, when no Esc follows or CTRL-D starts it */
	int last = 0;
	if (n > 0 && (k[n - 1] == '0' || k[n - 1] == '^') && (no_esc || (k[0] == 4 && count > 1))) last = k[--n];
	int *b = malloc(sizeof(int) * (size_t)((n + 4) * (count > 0 ? count : 1) + 2));
	if (!b) return 0;
	long m = 0;
	if (c) b[m++] = c;
	do {
		memcpy(b + m, k, sizeof(int) * (size_t)n); m += n;
		if (last) b[m++] = 22;
		if (last == '0') { b[m++] = '0'; b[m++] = '4'; b[m++] = '8'; }
		else if (last) b[m++] = '^';
	} while (--count > 0);
	if (!no_esc) b[m++] = KEY_ESC;
	stuff(b, m);
	free(b);
	return 1;
}

/* the keys of "." (get_last_insert_save(): without the final Esc) */
long last_insert_keys(const int **k)
{
	if (!last_ins) return -1;
	*k = last_ins + last_ins_skip;
	long n = last_ins_n - last_ins_skip;
	if (n > 0 && (*k)[n - 1] == KEY_ESC) n--;
	return n;
}

/* set_last_insert(): after "r", ". holds the character */
void set_last_insert(int c)
{
	free(last_ins);
	last_ins = malloc(sizeof(int) * 3);
	last_ins_n = last_ins_skip = 0;
	if (!last_ins) return;
	if (c < ' ' || c == 0x7f) last_ins[last_ins_n++] = 22;
	last_ins[last_ins_n++] = c;
	last_ins[last_ins_n++] = KEY_ESC;
}
