/* keys_feed(): one typed key, then the keys it stuffed.  Neovim reads keys
   from its stuff buffers (readbuf1, then readbuf2: commands like x that
   stand for others, ".", an insert's count) before the typeahead holding
   executed registers (@a), and those before what the user types next
   (vgetorpeek()).  Typed keys alone sync undo and are recorded (gotchars(),
   may_sync_undo()); a beep drops the stuffed and executed keys
   (beep_flush()). */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "git.h"
#include "k.h"
#include "kitten.h"
#include "loop.h"
#include "snip.h"
#include "tools.h"
#include "uni.h"

Editor *E;
int key_stuffed;

typedef struct { int *k; long head, n, cap; } Queue;
static Queue rb1, rb2, tb;

static void q_room(Queue *q, long n)
{
	if (q->head + q->n + n <= q->cap) return;
	if (q->n) memmove(q->k, q->k + q->head, sizeof(int) * (size_t)q->n);
	q->head = 0;
	if (q->n + n > q->cap) {
		long c = (q->n + n) * 2 + 16;
		int *b = realloc(q->k, sizeof(int) * (size_t)c);
		if (!b) return;
		q->k = b;
		q->cap = c;
	}
}

static void q_add(Queue *q, const int *k, long n)
{
	if (n <= 0) return;
	q_room(q, n);
	if (q->head + q->n + n > q->cap) return;
	memcpy(q->k + q->head + q->n, k, sizeof(int) * (size_t)n);
	q->n += n;
}

void stuff(const int *k, long n) { q_add(&rb1, k, n); }
void stuff_redo(const int *k, long n) { q_add(&rb2, k, n); }
void stuff_key(int k) { q_add(&rb1, &k, 1); }

void stuff_str(const char *s, long n)
{
	for (long i = 0; i < n; i++) stuff_key((unsigned char)s[i]);
}

void stuff_num(long n)
{
	char b[24];
	int i = 0;
	do b[i++] = (char)('0' + n % 10); while (n /= 10);
	while (i) stuff_key(b[--i]);
}

/* ins_typebuf(): an executed register goes before the rest of the typeahead */
void typebuf_ins(const int *k, long n)
{
	if (n <= 0) return;
	if (tb.head < n) {
		q_room(&tb, n);
		if (tb.head + tb.n + n > tb.cap) return;
		memmove(tb.k + tb.head + n, tb.k + tb.head, sizeof(int) * (size_t)tb.n);
		tb.head += n;
	}
	tb.head -= n;
	tb.n += n;
	memcpy(tb.k + tb.head, k, sizeof(int) * (size_t)n);
}

int stuff_empty(void) { return !rb1.n && !rb2.n; }

void beep(void)
{
	rb1.n = rb2.n = tb.n = 0;
	beeped = 1;
}

static int next(int *k)
{
	/* check_end_reg_executing(): a read with none of the register's keys
	   left in the typeahead ends its execution */
	if (!tb.n) E->ks.executing = 0;
	Queue *q = rb1.n ? &rb1 : rb2.n ? &rb2 : tb.n ? &tb : NULL;
	if (!q) return 0;
	*k = q->k[q->head++];
	q->n--;
	key_stuffed = q != &tb;
	return 1;
}

static void cmdline_key(int k);

static void key(int k, int pending)
{
	if (sub_asking) { sub_key(k); return; }
	switch (E->mode) {
	case MODE_INSERT: insert_key(k, pending); break;
	case MODE_CMDLINE: cmdline_key(k); break;
	default: normal_key(k); return;
	}
	/* normal_check() after edit() or the command line returns */
	if (E->mode == MODE_NORMAL && stuff_empty()) curs_columns();
}

/* ---- Davy's maps (FEATURES.md 4): nnoremap <Space>ff .. and - ---- */

/* only those whose targets exist; the SPC g and SPC h ones are git.h's.
   Each has the description which-key shows (my-nvim-keymaps.tsv's where
   it has one); one starting with + names a group, which-key's row for
   a prefix of the maps after it, and is never run.  In which-key's order
   (alphanum: by key, a lower-case letter before its capital) */
static const struct { const char *lhs, *desc; } maps[] = {
	{ " ,", "Buffers" },
	/* Emacs's C-x 5 frame keys, as SPC 5 (C-x is Vim's decrement) */
	{ " 5", "+frame" }, { " 50", "Delete frame" }, { " 51", "Delete other frames" }, { " 52", "New frame" },
	{ " 5f", "Find file in new frame" }, { " 5m", "Move window to frame" }, { " 5n", "Frame letter" }, { " 5o", "Other frame" },
	{ " c", "+compile" }, { " cc", "Compile" }, { " cC", "Recompile" },
	{ " D", "Dashboard" },
	{ " e", "File Explorer" },
	{ " f", "+find" }, { " ff", "Find Files" }, { " fg", "Grep" }, { " fr", "Registers" },
	{ " g", "+git" }, { " gb", "Git branches" }, { " gB", "Open on GitHub" }, { " gd", "Git diff (hunks)" },
	{ " gg", "Neogit (magit)" }, { " gl", "Git log" }, { " gL", "Git log (file)" }, { " gs", "Git status" },
	{ " h", "+hunks" }, { " hp", "Preview hunk" }, { " hr", "Reset hunk" }, { " hs", "Stage hunk" },
	{ " hS", "Stage buffer" }, { " hu", "Undo stage hunk" },
	{ " i", "+insert" }, { " is", "Snippet" },
	{ " p", "+project" }, { " pv", "File manager (oil)" },
	{ " u", "+toggle" }, { " uk", "Toggle kitten on save" }, { " up", "Toggle pretty entities" },
	{ "-", "Open parent directory (oil)" },
	{ "cit", "org next todo state" }, { "ciT", "org prev todo state" },  /* nvim-orgmode's, buffer-local to .org */
};
enum { NMAPS = sizeof maps / sizeof *maps };
static int lead[3], nlead;   /* the keys typed of a map not yet complete */
static int frame;            /* SPC 5 m or n typed: the next key is a frame's number or letter */
static int hint;             /* which-key's panel is shown */

/* m is a map of this buffer and mode: in Visual mode only SPC i s, a
   snippet around the area (package 19's, agreed), and its group */
static int usable(const char *m)
{
	return (*m != 'c' || is_ft(".org")) && (!E->ks.visual || !strncmp(m, " is", strlen(m)));
}

/* how many of the typed keys m starts with */
static int typed(const char *m)
{
	int j = 0;
	while (j < nlead && m[j] == lead[j]) j++;
	return j;
}

/* maps[i] is one key more than the keys typed: a row of the panel */
static int child(int i)
{
	const char *m = maps[i].lhs;
	return usable(m) && typed(m) == nlead && m[nlead] && !m[nlead + 1];
}

/* which-key.nvim's delay (200 ms, its default and Davy's): typed fast, a
   map never shows the panel; once shown, it follows each key at once */
static void hint_show(void *ctx) { (void)ctx; hint = 1; core_changed = 1; }
static void hint_end(void) { hint = 0; loop_timer(hint_show, NULL, -1); }

/* which-key.nvim's sort (config.lua's sort, then view.lua's natural and
   case): maps before groups, letters and digits before the rest, then the
   key ignoring case, lower case first */
static int rank(int i)
{
	unsigned char k = (unsigned char)maps[i].lhs[nlead];
	return (*maps[i].desc == '+') * 1024 + !isalnum(k) * 512 + tolower(k) * 2 + (isupper(k) != 0);
}

/* which-key.nvim's classic panel, with its defaults (view.lua's show()):
   the full width at the bottom, above the command line; a blank row, the
   maps and groups under the typed keys in columns filled one after the
   other (at least 2 rows; each column at least 20 cells wide, 3 apart,
   after 2 of padding: "key ➜ description", a group's in WhichKeyGroup), a
   blank row, then the trail of keys typed (a group by its name) and the
   help, centred.  Only after Space, so a c pausing in an Org buffer is
   Vim's c with nothing over it.  No allocation. */
void keys_draw(Editor *e, Grid *g)
{
	if (!hint) return;
	E = e;
	int n = 0, w = 20;
	for (int i = 0; i < NMAPS; i++)
		if (child(i)) {
			int iw = 4 + grid_width(maps[i].desc, strlen(maps[i].desc));
			if (iw > w) w = iw;
			n++;
		}
	int cols = g->cols / (w + 3) > 1 ? g->cols / (w + 3) : 1, h = (n + cols - 1) / cols;
	if (h < 2) h = 2;
	if (h > g->rows - 4) h = g->rows - 4;
	int top = g->rows - h - 4, last = g->rows - 2;
	/* which-key's no_overlap (view.lua's check_overlap()): over the
	   cursor's row or the one above it, the panel starts 2 rows below the
	   cursor, at least 4 high, and Neovim moves the float up to fit
	   (nvim_open_win's fixed off); the rows that no longer fit are cut */
	if (g->crow >= top - 1 && g->crow <= last) {
		last = g->rows - 1;
		top = g->crow + 2 < g->rows - 4 ? g->crow + 2 : g->rows - 4;
		h = last - top - 2;
	}
	if (h < 1 || top < 0) return;   /* a screen under 4 rows: no room */
	w = g->cols / cols;
	grid_blank(g, top, 0, h + 3, g->cols);
	for (int k = 0, prev = -1; k < n && k / h < cols; k++) {
		int b = -1;
		for (int i = 0; i < NMAPS; i++)
			if (child(i) && rank(i) > prev && (b < 0 || rank(i) < rank(b))) b = i;
		prev = rank(b);
		int c = 2 + 3 + k / h * w, r = top + 1 + k % h;
		const char *d = maps[b].desc;
		char key[2] = { maps[b].lhs[nlead], 0 };
		c = grid_str(g, r, c, key, FACE_NORMAL_FLOAT);
		c = grid_str(g, r, c, " \xe2\x9e\x9c ", FACE_WK_SEPARATOR);
		grid_str(g, r, c, d, *d == '+' ? FACE_WK_GROUP : FACE_WK_DESC);
	}
	int c = 1;
	for (int i = 0; i < nlead; i++) {
		char key[2] = { (char)lead[i], 0 };
		const char *s = lead[i] == ' ' ? "SPC" : key;
		for (int j = 0; j < NMAPS; j++)
			if (typed(maps[j].lhs) > i && !maps[j].lhs[i + 1]) s = maps[j].desc;
		c = grid_str(g, last, c, i ? " \xc2\xbb " : "", FACE_WK_SEPARATOR);
		c = grid_str(g, last, c, s, FACE_WK_GROUP);
	}
	c = (g->cols - 22) / 2 > c ? (g->cols - 22) / 2 : c;
	c = grid_str(g, last, c, "<esc>", FACE_NORMAL_FLOAT);
	c = grid_str(g, last, c, " close  ", FACE_WK_SEPARATOR);
	c = grid_str(g, last, c, "<bs>", FACE_NORMAL_FLOAT);
	grid_str(g, last, c, " back", FACE_WK_SEPARATOR);
	for (int i = top * g->cols; i < (last + 1) * g->cols; i++) g->c[i].bg = FACE_NORMAL_FLOAT;
}

/* snacks' :Dashboard, also SPC D: the start screen on a new empty buffer
   (:enew) (package 5's, agreed) */
static void dashboard(Editor *e) { if (!core_edit_file(e, "", 0)) tools_dash(e, 0); }

/* A map completed runs; one that may still be
   completed waits (a lone SPC as with which-key, without 'timeoutlen');
   otherwise the first key runs unmapped and the rest are checked again
   (Neovim's vgetorpeek() when no map matches) */
/* SPC 5 n {letter}: one SPC 5 has no key of its own and no other frame has */
static int frame_key(Editor *e, int k)
{
	if (k < 'a' || k > 'z' || strchr("fmno", k)) return -1;
	for (const Tab *t = e->tabs; t; t = t->next) if (t->key == k && t != e->tab) return -1;
	e->tab->key = (char)k;
	return 0;
}

static int map(Editor *e, int k)
{
	if (frame) {
		int f = frame;
		frame = e->ks.pending = 0;
		if (f == 'm' ? k < '1' || k > '9' || core_win_to_tab(e, k - '0') : frame_key(e, k)) beep();
		return 1;
	}
	if (!nlead && (e->mode != MODE_NORMAL || !normal_mappable(0))) return 0;
	E = e;   /* for is_ft() and the maps that edit */
	/* after Space, which-key's <Esc> closes (and ends a count, a register
	   or Visual mode, as Esc does), <BS> goes up a level; from the first
	   which-key goes to the root, the list of every Normal key, which has
	   no descriptions here, so it closes */
	if (nlead && lead[0] == ' ' && (k == KEY_ESC || k == KEY_BS)) {
		nlead = k == KEY_BS ? nlead - 1 : 0;
		if (nlead) return 1;
		hint_end();
		keys_feed(e, KEY_ESC, 0);
		return 1;
	}
	lead[nlead++] = k;
	if (nlead == 3 && lead[0] == ' ' && lead[1] == '5') {   /* SPC 5 {letter}: that frame */
		long n = 1;
		for (Tab *t = e->tabs; t; t = t->next, n++)
			if (t->key == k) {
				nlead = e->ks.pending = 0;
				hint_end();
				normal_mappable(1);
				goto_tabpage(n);
				return 1;
			}
	}
	int part = 0;
	for (int i = 0; i < NMAPS; i++) {
		const char *m = maps[i].lhs;
		int j = typed(m);
		if (!usable(m) || j < nlead) continue;
		if (m[j]) { part = 1; continue; }
		if (*maps[i].desc == '+') continue;
		nlead = e->ks.pending = 0;
		hint_end();
		normal_mappable(1);
		undo_close(e->buf->undo);   /* a map is a command of its own: keys_feed() closes before each */
		switch (m[0] == '-' ? 'p' : !strcmp(m, " is") ? 's' : m[1]) {
		case 'f': tools_pick(e, m[2] == 'f' ? PICK_SMART : m[2] == 'g' ? PICK_GREP : PICK_REGISTERS); break;
		case ',': tools_pick(e, PICK_BUFFERS); break;
		case 'e': tools_pick(e, PICK_EXPLORER); break;
		case 'p': tools_oil(e, NULL); break;
		case 'u': if (m[2] == 'k') kitten_toggle(e);
		          else e->win->conceal = e->win->conceal ? 0 : 2;   /* Davy's org-toggle-pretty-entities */
		          break;
		case 'i': org_cycle(m[2] == 't' ? 1 : -1); break;
		case 'D': dashboard(e); break;
		/* C-x 5 2 make-frame, 5 f find-file-other-frame (the picker in a
		   new frame), 5 o other-frame, 5 0 delete-frame, 5 1
		   delete-other-frames; 5 m {n} moves the window to frame n
		   (DESIGN.md, New approaches) */
		case '5':
			switch (m[2]) {
			case '2': core_tabnew(e, NULL); break;
			case 'f': if (!core_tabnew(e, NULL)) tools_pick(e, PICK_SMART); break;
			case 'o': goto_tabpage(0); break;
			case '0': core_tabclose(e, NULL); break;
			case '1': core_tabonly(e); break;
			default: frame = m[2]; e->ks.pending = 1;
			}
			break;
		case 's': tools_pick(e, PICK_SNIPPETS); break;
		/* Davy's emacs SPC cc compile (prompts) and cC recompile */
		case 'c':
			if (m[2] == 'C') { tools_make(e, NULL); break; }
			/* Emacs's compile: the last command, to edit */
			char c[sizeof e->cmd];
			const char *l = tools_make_last();
			snprintf(c, sizeof c, "make %s", l ? l : "");
			cmdline(':', c);
			break;
		default: git_leader(e, m[1], m[2]);
		}
		return 1;
	}
	if (part) {
		if (lead[0] == ' ' && !hint) loop_timer(hint_show, NULL, 200);
		return e->ks.pending = 1;   /* keys_input() sends the next keys here first */
	}
	int n = nlead, l[3];
	memcpy(l, lead, sizeof l);
	nlead = 0;
	if (n == 1) return 0;
	hint_end();
	keys_feed(e, l[0], 1);
	/* the rest as typed: a picker, the dashboard or magit may take them
	   (Neovim checks them against buffer-local maps too) */
	for (int i = 1; i < n; i++) keys_input(e, l[i], i < n - 1);
	return 1;
}

void keys_input(Editor *e, int k, int pending)
{
	/* keys.h KEY_FOCUS: what is half done ends, as Esc would end it */
	if (k == KEY_FOCUS) {
		int m = nlead || frame;
		nlead = frame = 0;
		hint_end();
		if (e->mode == MODE_INSERT || e->ks.visual || (!m && (e->ks.pending || !normal_mappable(0)))) keys_feed(e, KEY_ESC, 0);
		else e->ks.pending = 0;
		return;
	}
	if (!(e->ks.pending ? map(e, k) : tools_key(e, k) || git_key(e, k) || snip_key(e, k, pending) || map(e, k))) keys_feed(e, k, pending);
}

/* what the core draws of Visual mode */
static void visual_sync(void)
{
	E->ks.visual = VIsual_active ? VIsual_mode : 0;
	E->ks.vline = (size_t)VIsual.lnum;
	E->ks.vcol = (size_t)VIsual.col;
}

void keys_visual(Editor *e, int mode, size_t line)
{
	E = e;
	buf_changed();
	if (!mode) { if (VIsual_active) end_visual_mode(); }
	else VIsual_active = 1, VIsual_mode = mode, VIsual = (Pos){ (long)line, 0, 0 };
	visual_sync();
}

void keys_feed(Editor *e, int k, int pending)
{
	/* the window's close button: :qa, as Neovim's GUIs do (E37 if
	   modified) */
	if (k == KEY_CLOSE) {
		if (sub_asking) E = e, sub_key(KEY_ESC), e->ks.pending = 0;   /* the question ends first, as Esc */
		core_close(e, 0);
		return;
	}
	E = e;
	buf_changed();
	if (B != op_buf) opmarks(1);
	/* :s with the c flag is one undo step, however many keys answer it */
	if (!sub_asking && ((E->mode != MODE_INSERT && E->mode != MODE_CMDLINE) || arrow_used)) undo_close(B->undo);
	if (reg_recording) record_key(k);
	key_stuffed = 0;
	E->ks.executing = 0;   /* typed: the typeahead holds no register keys */
	key(k, pending || rb1.n || rb2.n || tb.n);
	while (next(&k)) key(k, pending || rb1.n || rb2.n || tb.n);
	if (B == op_buf) opmarks(0);   /* not into a buffer the key went to */
	visual_sync();
	E->ks.pending = E->mode == MODE_CMDLINE || sub_asking || (E->mode != MODE_INSERT && normal_pending());
	core_update(e);
}

/* ---- the command line: ranges, :w :q :wq :x :Format ---- */

/* keys.h; only the current buffer, the one :w writes */
void keys_write_pre(Editor *e, Buf *b)
{
	char d[4096];
	size_t n = 0;
	E = e;
	int k = b == e->buf ? fmt_kind() : 0;
	const char *mark = k == 1 ? "sqldiff.toml" : ".slang";
	if (k) {
		if (B->path[0] != '/' && B->path[0] != '\\' && B->path[1] != ':') {
			if (!getcwd(d, sizeof d - 1)) d[0] = 0;
			n = strlen(d);
			if (n) d[n++] = '/';
		}
		size_t p = strlen(B->path);
		if (n + p + 13 < sizeof d) {
			memcpy(d + n, B->path, p);
			n += p;
			for (struct stat st;;) {
				while (n && d[n - 1] != '/' && d[n - 1] != '\\') n--;
				memcpy(d + n, mark, strlen(mark) + 1);
				if (!stat(d, &st)) { buf_format(0, text_len(B->text)); break; }
				if (n <= 1 || (n == 3 && d[1] == ':')) break;
				n--;
			}
		}
	}
}

/* one address and its offsets, 1-based, from line cur (Neovim's
   get_address()); NONE when there is none, FAIL after E20 (or a bare ', silent).  Past BIG a number
   saturates, where Neovim's overflows. */
static const long BIG = LONG_MAX / 4, NONE = LONG_MIN, FAIL = LONG_MIN + 1;
static long sat(long v) { return v > BIG ? BIG : v < -BIG ? -BIG : v; }
static long address(char **pp, long cur)
{
	char *p = *pp;
	long l = NONE;
	if (*p >= '0' && *p <= '9') l = sat(strtol(p, &p, 10));
	else if (*p == '.' || *p == '$') l = *p++ == '.' ? cur : nlines();
	else if (*p == '\'') {
		Pos m;
		if (!p[1] || !getmark((unsigned char)p[1], &m)) return FAIL;   /* get_address(): ' alone fails silently */
		l = m.lnum + 1;
		p += 2;
	}
	for (;;) {
		while (*p == ' ' || *p == '\t') p++;
		if (*p != '-' && *p != '+' && (*p < '0' || *p > '9')) break;
		int sign = *p == '-' ? -1 : 1;
		if (l == NONE) l = cur;
		p += *p == '-' || *p == '+';
		l = sat(l + sign * (*p >= '0' && *p <= '9' ? sat(strtol(p, &p, 10)) : 1));
	}
	*pp = p;
	return l;
}

static void ex(void)
{
	char *c = E->cmd, *p;
	E->cmd[E->cmdlen] = 0;
	set_last_cmdline(c);
	while (*c == ' ' || *c == ':') c++;
	/* the range (Neovim's parse_cmd_range()): % is 1,$; a missing address
	   next to a separator is the cursor's line; ";" moves the cursor to its
	   address, which the next one counts from */
	long cur = (long)W->line + 1, l1 = NONE, l2 = NONE, n = 0;
	for (p = c;; p++) {
		p += strspn(p, " \t");
		int all = *p == '%';
		long a = all ? (p++, nlines()) : address(&p, cur);
		if (a == FAIL) return;
		if (a == NONE && (n || *p == ',' || *p == ';')) a = cur;
		if (a == NONE) break;
		l1 = all ? 1 : n ? l2 : a, l2 = a;
		n++;
		if (*p == ';') W->line = (size_t)((cur = a > nlines() ? nlines() : a) < 1 ? 1 : cur) - 1, check_cursor_col();
		else if (*p != ',') break;
	}
	while (*p == ' ' || *p == '\t' || *p == ':') p++;
	/* :s[ubstitute] :& :~ (:&& is :& with the flag &), on the cursor's
	   line by default; line 0 is line 1 (no EX_ZEROR) */
	size_t k = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	/* :noh[lsearch], and :mak[e] {cmd} (tools.h) with the rest of the line its argument */
	int nohl = k >= 3 && k <= 10 && !strncmp(p, "nohlsearch", k) && !p[k];
	if (nohl || (k >= 3 && k <= 4 && !strncmp(p, "make", k) && (!p[k] || p[k] == ' ' || p[k] == '\t'))) {
		if (n) core_msg(E, "E481: No range allowed");
		else if (nohl) no_hlsearch = 1;
		else tools_make(E, p + k + strspn(p + k, " \t"));
		return;
	}
	if (k ? k <= 10 && !strncmp(p, "substitute", k) : (k = *p == '&' || *p == '~')) {
		if (!n) l1 = l2 = cur;
		if (l1 > l2) { core_msg(E, "E493: Backwards range given"); return; }
		if (l1 < 0 || l2 > nlines()) { core_msg(E, "E16: Invalid range"); return; }
		ex_sub(l1 > 0 ? l1 - 1 : 0, l2 > 0 ? l2 - 1 : 0, *p, p + k + strspn(p + k, " \t"));
		return;
	}
	static const char *const cmds[] = { "", "w", "w!", "q", "q!", "wq", "wq!", "x", "Format", "KittenPaw", "Dashboard" };
	int i = 0;
	while (i < 11 && strcmp(p, cmds[i])) i++;
	if (i == 11) { core_msg(E, "E492: Not an editor command: %s", c); return; }
	if (n && i) { core_msg(E, "E481: No range allowed"); return; }
	switch (i) {
	case 0:   /* a range alone goes to its last line (do_one_cmd()) */
		if (l2 < 0 && n) core_msg(E, "E16: Invalid range");
		else if (n) {
			W->line = (size_t)(l2 < 1 ? 1 : l2 > nlines() ? nlines() : l2) - 1;
			beginline(BL_SOL | BL_FIX);
		}
		break;
	case 1: case 2: core_write(E, B, i == 2); break;
	case 3: case 4: core_close(E, i == 4); break;
	case 5: case 6: if (!core_write(E, B, i == 6)) core_close(E, 0); break;
	case 7: if (!core_modified(B) || !core_write(E, B, 0)) core_close(E, 0); break;
	/* Davy's :Format (conform.nvim): the whole buffer */
	case 8: if (fmt_kind()) buf_format(0, text_len(B->text)); break;
	case 9: kitten_paw(E); break;
	default: dashboard(E);
	}
}

/* leaving the command line: / and ? wait in normal.c for K_PAT (entered)
   or another key (left) */
static void cmdline_end(int entered)
{
	E->mode = MODE_NORMAL;
	if (E->cmdtype != ':') normal_key(entered ? K_PAT : KEY_ESC);
	else if (entered) ex(), checkpcmark();
	restart_insert();
}

static void cmdline_key(int k)
{
	if (k == KEY_ESC || k == KEY_CTRL_C) { cmdline_end(0); return; }
	if (k == KEY_ENTER || k == '\n') { core_msg(E, ""); cmdline_end(1); return; }
	if (k == KEY_BS || k == 8) {
		if (!E->cmdlen) { cmdline_end(0); return; }
		E->cmdlen = E->cmdpos = uni_gprev((unsigned char *)E->cmd, E->cmdlen, E->cmdlen);
		return;
	}
	if (k < 0x20 || k >= KEY_UP || E->cmdlen + 4 >= sizeof E->cmd) return;
	char b[4];
	long n = key_to_bytes(k, b);
	memcpy(E->cmd + E->cmdlen, b, (size_t)n);
	E->cmdpos = E->cmdlen += (size_t)n;
}
