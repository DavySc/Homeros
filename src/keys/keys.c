/* keys_feed(): one typed key, then the keys it stuffed.  Neovim reads keys
   from its stuff buffers (readbuf1, then readbuf2: commands like x that
   stand for others, ".", an insert's count) before the typeahead holding
   executed registers (@a), and those before what the user types next
   (vgetorpeek()).  Typed keys alone sync undo and are recorded (gotchars(),
   may_sync_undo()); a beep drops the stuffed and executed keys
   (beep_flush()). */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "git.h"
#include "k.h"
#include "kitten.h"
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

/* only those whose targets exist; the SPC g and SPC h ones are git.h's */
static const char *const maps[] = {
	" ff", " fg", " fr", " ,", " e", " pv", "-", " D",
	" gg", " gs", " gl", " gL", " gd", " gb", " gB", " hs", " hr", " hS", " hu", " hp", " uk"
};
static int lead[3], nlead;   /* the keys typed of a map not yet complete */

/* snacks' :Dashboard, also SPC D: the start screen on a new empty buffer
   (:enew) (package 5's, agreed) */
static void dashboard(Editor *e) { if (!core_edit_file(e, "", 0)) tools_dash(e, 0); }

/* A map completed runs; one that may still be
   completed waits (a lone SPC as with which-key, without 'timeoutlen');
   otherwise the first key runs unmapped and the rest are checked again
   (Neovim's vgetorpeek() when no map matches) */
static int map(Editor *e, int k)
{
	if (!nlead && (e->mode != MODE_NORMAL || e->ks.visual || !normal_mappable(0))) return 0;
	lead[nlead++] = k;
	int part = 0;
	for (size_t i = 0; i < sizeof maps / sizeof *maps; i++) {
		const char *m = maps[i];
		int j = 0;
		while (j < nlead && m[j] == lead[j]) j++;
		if (j < nlead) continue;
		if (m[j]) { part = 1; continue; }
		nlead = e->ks.pending = 0;
		normal_mappable(1);
		switch (m[0] == '-' ? 'p' : m[1]) {
		case 'f': tools_pick(e, m[2] == 'f' ? PICK_SMART : m[2] == 'g' ? PICK_GREP : PICK_REGISTERS); break;
		case ',': tools_pick(e, PICK_BUFFERS); break;
		case 'e': tools_pick(e, PICK_EXPLORER); break;
		case 'p': tools_oil(e, NULL); break;
		case 'u': kitten_toggle(e); break;
		case 'D': dashboard(e); break;
		default: git_leader(e, m[1], m[2]);
		}
		return 1;
	}
	if (part) return e->ks.pending = 1;   /* keys_input() sends the next keys here first */
	int n = nlead, l[3];
	memcpy(l, lead, sizeof l);
	nlead = 0;
	if (n == 1) return 0;
	keys_feed(e, l[0], 1);
	/* the rest as typed: a picker, the dashboard or magit may take them
	   (Neovim checks them against buffer-local maps too) */
	for (int i = 1; i < n; i++) keys_input(e, l[i], i < n - 1);
	return 1;
}

void keys_input(Editor *e, int k, int pending)
{
	if (!(e->ks.pending ? map(e, k) : tools_key(e, k) || git_key(e, k) || map(e, k))) keys_feed(e, k, pending);
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
	/* :s with the c flag is one undo step, however many keys answer it */
	if (!sub_asking && ((E->mode != MODE_INSERT && E->mode != MODE_CMDLINE) || arrow_used)) undo_close(B->undo);
	if (reg_recording) record_key(k);
	key_stuffed = 0;
	E->ks.executing = 0;   /* typed: the typeahead holds no register keys */
	key(k, pending || rb1.n || rb2.n || tb.n);
	while (next(&k)) key(k, pending || rb1.n || rb2.n || tb.n);
	E->ks.visual = VIsual_active ? VIsual_mode : 0;
	E->ks.vline = (size_t)VIsual.lnum;
	E->ks.vcol = (size_t)VIsual.col;
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
	if (b == e->buf && is_sql()) {
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
				memcpy(d + n, "sqldiff.toml", 13);
				if (!stat(d, &st)) { sql_format(0, text_len(B->text)); break; }
				if (n <= 1 || (n == 3 && d[1] == ':')) break;
				n--;
			}
		}
	}
}

/* one address and its offsets, 1-based, from line cur (Neovim's
   get_address()); NONE when there is none, FAIL after E20.  Past BIG a number
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
		static const char sp[] = "[]<>.^\"";
		const char *q = p[1] ? strchr(sp, p[1]) : NULL;
		Mark m = p[1] == '\'' ? W->pcmark : p[1] >= 'a' && p[1] <= 'z' ? B->mark[p[1] - 'a'] : q ? B->mark[MARK_LBRACKET + (q - sp)] : (Mark){ 0 };
		if (!m.set) { core_msg(E, "E20: Mark not set"); return FAIL; }
		l = (long)m.line + 1;
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
	if (k >= 3 && k <= 10 && !strncmp(p, "nohlsearch", k) && !p[k]) {
		if (n) core_msg(E, "E481: No range allowed");
		else no_hlsearch = 1;
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
	case 8: if (is_sql()) sql_format(0, text_len(B->text)); break;
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
	else if (entered) ex();
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
