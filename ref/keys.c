/* Reference keys: the thin editor's set, each copying Neovim 0.12 with
   FEATURES.md's settings (autoindent, expandtab, shiftwidth and softtabstop
   4, smarttab, backspace=indent,eol,start, nostartofline).  The comments name
   the Neovim function a rule comes from; tools/conform.py checks them. */
#include <string.h>
#include "core.h"
#include "uni.h"

enum { STS = 4 };

typedef struct { size_t line, col; } Pos;

static Pos cur(Editor *e) { return (Pos){ e->win->line, e->win->col }; }
static void set_want(Editor *e) { core_set_want(e); }

/* Character class for word motions (cls() in textobject.c): 0 blank, 1
   punctuation, 2 word.  Above Latin-1 everything is a word here; package 5
   takes Vim's utf_class table. */
static int cls(Editor *e, Pos p)
{
	size_t n;
	const unsigned char *s = core_line(e, p.line, &n);
	if (p.col >= n) return 0;
	unsigned c = uni_cp(s, n, p.col);
	if (c == ' ' || c == '\t' || c == 0xa0) return 0;
	if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ? 2 : 1;
	if (c < 0x100) return c >= 0xc0 ? 2 : 1;   /* iskeyword=@,48-57,_,192-255 */
	return 2;
}

/* inc() and dec() in Neovim's misc: step one character, through the end of
   line (col == len) and across lines.  inc: 0 same line, 2 onto the end of
   line, 1 next line, -1 end of buffer.  dec: 0, 1 previous line, -1 start. */
static int inc(Editor *e, Pos *p)
{
	size_t n;
	const unsigned char *s = core_line(e, p->line, &n);
	if (p->col < n) { p->col += uni_len(s, n, p->col); return p->col < n ? 0 : 2; }
	if (p->line + 1 < core_lines(e)) { p->line++; p->col = 0; return 1; }
	return -1;
}

static int dec(Editor *e, Pos *p)
{
	size_t n;
	const unsigned char *s = core_line(e, p->line, &n);
	if (p->col > 0) { p->col = uni_prev(s, n, p->col); return 0; }
	if (p->line > 0) { p->line--; p->col = core_line_len(e, p->line); return 1; }
	return -1;
}

static int empty_line(Editor *e, Pos p) { return p.col == 0 && core_line_len(e, p.line) == 0; }

/* fwd_word(), eol false */
static void word_fwd(Editor *e, unsigned long count)
{
	Pos p = cur(e);
	while (count--) {
		int sc = cls(e, p), last = p.line + 1 == core_lines(e), i = inc(e, &p);
		if (i == -1 || (i >= 1 && last)) break;
		if (sc) while (cls(e, p) == sc) if (inc(e, &p) == -1) goto done;
		while (cls(e, p) == 0 && !empty_line(e, p)) if (inc(e, &p) == -1) goto done;
	}
done:
	e->win->line = p.line; e->win->col = p.col;
}

/* bck_word(), stop false */
static void word_back(Editor *e, unsigned long count)
{
	Pos p = cur(e);
	while (count--) {
		if (dec(e, &p) == -1) break;
		while (cls(e, p) == 0) {
			if (empty_line(e, p)) goto next;
			if (dec(e, &p) == -1) goto done;
		}
		for (int c = cls(e, p); cls(e, p) == c;) if (dec(e, &p) == -1) goto done;
		inc(e, &p);
	next:;
	}
done:
	e->win->line = p.line; e->win->col = p.col;
}

static void go_line(Editor *e, size_t l)
{
	e->win->line = l;
	e->win->col = core_col_at(e, l, e->win->want, e->mode == MODE_INSERT);
}

static size_t indent_cols(Editor *e, size_t l, size_t upto)
{
	size_t n, i = 0;
	const unsigned char *s = core_line(e, l, &n);
	while (i < n && i < upto && (s[i] == ' ' || s[i] == '\t')) i++;
	return core_vcol(e, l, i);
}

static void spaces(char *b, size_t n) { memset(b, ' ', n); }

/* o and O: open_line() with autoindent; the new line copies the indent as
   spaces (expandtab) and remembers it was not typed (did_ai). */
static void open_line(Editor *e, int below)
{
	char b[512];
	size_t ind = indent_cols(e, e->win->line, (size_t)-1);
	if (ind > sizeof b - 1) ind = sizeof b - 1;
	spaces(b, ind);
	b[ind] = '\n';
	size_t l = below ? e->win->line + 1 : e->win->line;
	if (core_edit(e, core_line_start(e, l), 0, b, ind + 1)) return;
	e->win->line = l; e->win->col = ind;
	e->mode = MODE_INSERT;
	e->ai = 1;
	set_want(e);
}

/* Enter in insert mode: open_line() FORWARD.  The indent is that of the text
   before the cursor; blanks after the cursor are dropped; after an unused
   autoindent the old line loses its trailing blanks (trunc_line). */
static void enter(Editor *e)
{
	char b[512];
	size_t n;
	const unsigned char *s = core_line(e, e->win->line, &n);
	size_t ls = core_line_start(e, e->win->line), ind = indent_cols(e, e->win->line, e->win->col);
	size_t a = e->win->col, z = e->win->col;
	if (e->ai) while (a > 0 && (s[a - 1] == ' ' || s[a - 1] == '\t')) a--;
	while (z < n && (s[z] == ' ' || s[z] == '\t')) z++;
	if (ind > sizeof b - 1) ind = sizeof b - 1;
	b[0] = '\n';
	spaces(b + 1, ind);
	if (core_edit(e, ls + a, z - a, b, ind + 1)) return;
	e->ai = 1;
	e->win->line++; e->win->col = ind;
	set_want(e);
}

static int blank(unsigned char c) { return c == ' ' || c == '\t'; }

/* Backspace (ins_bs()): backspace=indent,eol,start.  In the indent (smarttab)
   or over blanks (softtabstop) it deletes back to the previous multiple of
   4, unless the space was typed on its own (KeyState.space) outside the indent. */
static void backspace(Editor *e)
{
	size_t n;
	const unsigned char *s = core_line(e, e->win->line, &n);
	size_t ls = core_line_start(e, e->win->line), c = e->win->col, a = c;
	if (c == 0) {
		if (e->win->line == 0) return;
		size_t pl = core_line_len(e, e->win->line - 1);
		if (core_edit(e, ls - 1, 1, "", 0)) return;
		e->win->line--; e->win->col = pl;
		e->ai = 0;
		return;
	}
	while (a > 0 && blank(s[a - 1])) a--;
	if (a == 0 || s[c - 1] == '\t' || (s[c - 1] == ' ' && !e->ks.space)) {
		e->ks.space = 0;
		size_t v = core_vcol(e, e->win->line, c), want = v - 1 - (v - 1) % STS;
		a = c;
		while (a > 0 && blank(s[a - 1])) a--;
		while (a < c && core_vcol(e, e->win->line, a + 1) <= want) a++;   /* blanks are one byte */
		size_t sv = core_vcol(e, e->win->line, a), add = sv < want ? want - sv : 0;
		char b[STS];
		spaces(b, add);
		if (core_edit(e, ls + a, c - a, b, add)) return;
		e->win->col = a + add;
	} else {
		size_t p = uni_prev(s, n, c);
		if (core_edit(e, ls + p, c - p, "", 0)) return;
		e->win->col = p;
	}
	if (e->win->col <= 1) e->ai = 0;
}

static void tab(Editor *e)
{
	char b[STS];
	size_t k = STS - core_vcol(e, e->win->line, e->win->col) % STS;
	spaces(b, k);
	e->ai = 0;
	e->ks.space = 0;
	if (!core_edit(e, core_line_start(e, e->win->line) + e->win->col, 0, b, k)) e->win->col += k;
}

static size_t utf8(int k, char *b)
{
	unsigned c = (unsigned)k;
	if (c < 0x80) { b[0] = (char)c; return 1; }
	if (c < 0x800) { b[0] = (char)(0xC0 + c / 64); b[1] = (char)(0x80 + c % 64); return 2; }
	if (c < 0x10000) { b[0] = (char)(0xE0 + c / 4096); b[1] = (char)(0x80 + c / 64 % 64); b[2] = (char)(0x80 + c % 64); return 3; }
	b[0] = (char)(0xF0 + c / 262144); b[1] = (char)(0x80 + c / 4096 % 64);
	b[2] = (char)(0x80 + c / 64 % 64); b[3] = (char)(0x80 + c % 64);
	return 4;
}

/* Esc: drop an unused autoindent, step back onto a character, end the undo
   group (stop_insert(), ins_esc()). */
/* Esc (ins_esc(), stop_insert()).  After an unused autoindent, blanks are
   deleted from the cursor (or the end of line) leftwards; if that moved the
   cursor, curswant keeps its old value.  Then step back onto a character. */
static void insert_end(Editor *e)
{
	size_t n, c = e->win->col, start = e->win->col;
	const unsigned char *s = core_line(e, e->win->line, &n);
	if (e->ai) {
		size_t ls = core_line_start(e, e->win->line);
		for (;;) {
			if (c >= n && c > 0) c = uni_prev(s, n, c);
			if (c >= n || !blank(s[c])) break;
			if (core_edit(e, ls + c, 1, "", 0)) break;
			s = core_line(e, e->win->line, &n);
		}
		if (c < start && c < n && c + 1 >= n) c++;   /* back on the end of line */
		e->win->col = c;
	}
	e->ai = 0;
	e->mode = MODE_NORMAL;
	if (e->win->col > 0) e->win->col = uni_prev(s, n, e->win->col);
	core_clamp(e);
	if (c == start) set_want(e);
	undo_close(e->buf->undo);
}

static void insert_key(Editor *e, int k, int pending)
{
	char b[4];
	switch (k) {
	case KEY_ESC: case KEY_CTRL_C: insert_end(e); return;
	case KEY_ENTER: enter(e); e->ks.space = 0; break;
	case KEY_BS: case 8: backspace(e); break;
	case KEY_TAB: tab(e); break;
	case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
		undo_close(e->buf->undo);    /* a cursor move splits the insert (Neovim) */
		e->ai = 0;
		if (k == KEY_LEFT && e->win->col > 0) { size_t n; const unsigned char *s = core_line(e, e->win->line, &n); e->win->col = uni_prev(s, n, e->win->col); }
		if (k == KEY_RIGHT) { size_t n; const unsigned char *s = core_line(e, e->win->line, &n); if (e->win->col < n) e->win->col += uni_len(s, n, e->win->col); }
		if (k == KEY_UP && e->win->line > 0) go_line(e, e->win->line - 1);
		if (k == KEY_DOWN && e->win->line + 1 < core_lines(e)) go_line(e, e->win->line + 1);
		if (k == KEY_LEFT || k == KEY_RIGHT) set_want(e);
		return;
	default:
		if (k < 0x20 || k >= KEY_UP) return;
		e->ai = 0;
		/* insertchar() takes queued one-byte characters (not 0 or ^) as one
		   string, so a space among them is not "inserted_space" */
		int plain = k < 0x7f && k != '0' && k != '^';
		if (!(e->ks.batch && plain) && k == ' ') e->ks.space = 1;
		e->ks.batch = plain && pending;
		size_t n = utf8(k, b);
		if (!core_edit(e, core_line_start(e, e->win->line) + e->win->col, 0, b, n)) e->win->col += n;
	}
	set_want(e);
}

static void ex(Editor *e)
{
	char *c = e->cmd;
	e->cmd[e->cmdlen] = 0;
	while (*c == ' ' || *c == ':') c++;
	if (!strcmp(c, "w") || !strcmp(c, "w!")) core_write(e, e->buf, c[1] == '!');
	else if (!strcmp(c, "q") || !strcmp(c, "q!")) core_close(e, c[1] == '!');
	else if (!strcmp(c, "wq") || !strcmp(c, "wq!")) { if (!core_write(e, e->buf, c[2] == '!')) core_close(e, 0); }
	else if (!strcmp(c, "x")) { if (!core_modified(e->buf) || !core_write(e, e->buf, 0)) core_close(e, 0); }
	else if (*c) core_msg(e, "E492: Not an editor command: %s", c);
}

static void cmdline_key(Editor *e, int k)
{
	if (k == KEY_ESC || k == KEY_CTRL_C) { e->mode = MODE_NORMAL; return; }
	if (k == KEY_ENTER) { e->mode = MODE_NORMAL; core_msg(e, ""); ex(e); return; }
	if (k == KEY_BS || k == 8) {
		if (!e->cmdlen) { e->mode = MODE_NORMAL; return; }
		e->cmdlen = e->cmdpos = uni_prev((unsigned char *)e->cmd, e->cmdlen, e->cmdlen);
		return;
	}
	if (k < 0x20 || k >= KEY_UP || e->cmdlen + 4 >= sizeof e->cmd) return;
	e->cmdpos = e->cmdlen += utf8(k, e->cmd + e->cmdlen);
}

static void empty_step(Editor *e)
{
	UndoCursor c = { e->win->line, e->win->col, e->buf->flags };
	undo_record(e->buf->undo, core_line_start(e, e->win->line), "", 0, "", 0, c, e->buf->flags);
}

/* An operator on an empty buffer (CORE_EMPTY): op_delete() returns early, yet
   the undo history changes.  Measured in nvim --headless (15 sequences): at
   the oldest state it leaves an empty step, which also drops the redo
   branch; after a change it leaves nothing.  The mechanism is not
   understood (hypothesis: undo sync inside typeahead); package 5 revisits. */
static void empty_op(Editor *e)
{
	if (!undo_seq(e->buf->undo)) empty_step(e);
}

static void insert_at(Editor *e, size_t col)
{
	e->win->col = col;
	e->mode = MODE_INSERT;
	e->ai = 0;
	e->ks.space = 0;
}

/* the pending count and operator ('d' or 'g'): global, as in Neovim */
static unsigned long count;
static int pending_op;

static void normal_key(Editor *e, int k)
{
	if (k >= '0' && k <= '9' && (k != '0' || count) && pending_op != 'g') {
		count = count * 10 + (unsigned long)(k - '0');
		if (count > 99999999) count = 99999999;
		return;
	}
	unsigned long n = count ? count : 1, given = count;
	int op = pending_op;
	count = 0;
	pending_op = 0;
	size_t nl = core_lines(e), len;
	const unsigned char *s = core_line(e, e->win->line, &len);
	if (op == 'g') {
		if (k == 'g') go_line(e, given && given <= nl ? given - 1 : given ? nl - 1 : 0);
		return;
	}
	if (op == 'd') {
		if (k != 'd' || (n > 1 && e->win->line + 1 == nl)) return;   /* cursor_down() fails */
		if (e->buf->flags & CORE_EMPTY) { empty_op(e); return; }
		size_t a = e->win->line;
		core_delete_lines(e, a, a + n < nl ? a + n : nl);
		/* nostartofline: back to curswant, which then follows the cursor */
		go_line(e, a < core_lines(e) ? a : core_lines(e) - 1);
		set_want(e);
		return;
	}
	switch (k) {
	case 'd': case 'g': pending_op = k; count = given; return;
	case 'h': case KEY_LEFT:    /* curswant changes only if the cursor moves (oneleft()) */
		if (!e->win->col) return;
		while (n-- && e->win->col > 0) e->win->col = uni_prev(s, len, e->win->col);
		set_want(e); return;
	case 'l': case KEY_RIGHT:
		if (e->win->col >= len || e->win->col + uni_len(s, len, e->win->col) >= len) return;
		while (n-- && e->win->col < len && e->win->col + uni_len(s, len, e->win->col) < len) e->win->col += uni_len(s, len, e->win->col);
		set_want(e); return;
	case 'j': case KEY_DOWN:
		if (e->win->line + 1 < nl) go_line(e, e->win->line + n < nl ? e->win->line + n : nl - 1);
		return;
	case 'k': case KEY_UP:
		if (e->win->line > 0) go_line(e, e->win->line > n ? e->win->line - n : 0);
		return;
	case '0': e->win->col = 0; set_want(e); return;
	case '^': e->win->col = core_first_nonblank(e, e->win->line); set_want(e); return;
	case '$':
		e->win->want = WANT_EOL;
		if (n > 1 && e->win->line + 1 == nl) return;
		go_line(e, e->win->line + n - 1 < nl ? e->win->line + n - 1 : nl - 1);
		return;
	case 'w': word_fwd(e, n); core_clamp(e); set_want(e); return;
	case 'b': word_back(e, n); core_clamp(e); set_want(e); return;
	case 'G': go_line(e, given && given <= nl ? given - 1 : nl - 1); return;
	case 'x':
		/* on an empty line x deletes nothing but still makes an undo step
		   (op_delete() calls u_save_cursor()), so u then undoes nothing */
		if (!len) {
			if (e->buf->flags & CORE_EMPTY) empty_op(e);
			else empty_step(e);
			set_want(e);
			return;
		}
		{
			size_t z = e->win->col;
			while (n-- && z < len) z += uni_len(s, len, z);
			core_edit(e, core_line_start(e, e->win->line) + e->win->col, z - e->win->col, "", 0);
		}
		core_clamp(e); set_want(e); return;
	case 'i': insert_at(e, e->win->col); return;
	case 'a': insert_at(e, len ? e->win->col + uni_len(s, len, e->win->col) : 0); return;
	case 'I': e->mode = MODE_INSERT; insert_at(e, core_first_nonblank(e, e->win->line)); return;
	case 'A': insert_at(e, len); return;
	case 'o': case 'O': e->ks.space = 0; open_line(e, k == 'o'); return;
	case 'u': case KEY_CTRL_R: {
		/* nv_kundo() asks for curswant to be recomputed from the cached
		   virtual column, which stays valid if no step invalidated it and
		   the cursor ends where it started (check_cursor_moved()): curswant
		   is then the column in the text before the undo (measured, nvim
		   0.12, "jxu" on "b  a\nc\tfoo" gives 7) */
		size_t l0 = e->win->line, c0 = e->win->col, w0 = e->win->want, v0;
		int inval = 0;
		set_want(e);
		v0 = e->win->want;
		e->win->want = w0;
		/* u_doit() stops at the first step that finds nothing */
		for (int r; n-- && (r = core_undo(e, k == KEY_CTRL_R)) >= 0;) inval |= r;
		if (!inval && e->win->line == l0 && e->win->col == c0) e->win->want = v0;
		else set_want(e);
		return;
	}
	case ':': e->mode = MODE_CMDLINE; e->cmdtype = ':'; e->cmdlen = e->cmdpos = 0; return;
	}
}

void keys_feed(Editor *e, int k, int pending)
{
	switch (e->mode) {
	case MODE_INSERT:
		if (k < 0x20 || k >= 0x7f) e->ks.batch = 0;
		insert_key(e, k, pending);
		break;
	case MODE_CMDLINE: cmdline_key(e, k); break;
	default:
		normal_key(e, k);
		if (e->mode == MODE_NORMAL) undo_close(e->buf->undo);
	}
	core_update(e);
}
