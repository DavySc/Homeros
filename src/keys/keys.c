/* keys_feed(): one typed key, then the keys it stuffed.  Neovim reads keys
   from its stuff buffers (readbuf1, then readbuf2: commands like x that
   stand for others, ".", an insert's count) before the typeahead holding
   executed registers (@a), and those before what the user types next
   (vgetorpeek()).  Typed keys alone sync undo and are recorded (gotchars(),
   may_sync_undo()); a beep drops the stuffed and executed keys
   (beep_flush()). */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "git.h"
#include "k.h"
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
	if (!tb.n) reg_executing = 0;
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
	" ff", " fg", " fr", " ,", " e", " pv", "-",
	" gg", " gs", " gl", " gL", " gd", " gb", " gB", " hs", " hr", " hS", " hu", " hp"
};
static int lead[3], nlead;   /* the keys typed of a map not yet complete */

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
	if (k == KEY_CLOSE) { core_close(e, 0); return; }
	E = e;
	buf_changed();
	if ((E->mode != MODE_INSERT && E->mode != MODE_CMDLINE) || arrow_used) undo_close(B->undo);
	if (reg_recording) record_key(k);
	key_stuffed = 0;
	reg_executing = 0;   /* typed: the typeahead holds no register keys */
	key(k, pending || rb1.n || rb2.n || tb.n);
	while (next(&k)) key(k, pending || rb1.n || rb2.n || tb.n);
	E->ks.visual = VIsual_active ? VIsual_mode : 0;
	E->ks.vline = (size_t)VIsual.lnum;
	E->ks.vcol = (size_t)VIsual.col;
	E->ks.pending = E->mode == MODE_CMDLINE || (E->mode != MODE_INSERT && normal_pending());
	core_update(e);
}

/* ---- the command line: :w :q :wq :x :Format ---- */

/* Davy's BufWritePre for .sql (after/ftplugin/sql.lua): format first when
   a sqldiff.toml is in the file's directory or one above it */
static int ex_write(int force)
{
	char d[4096];
	size_t n = 0;
	if (is_sql()) {
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
	return core_write(E, B, force);
}

static void ex(void)
{
	char *c = E->cmd;
	E->cmd[E->cmdlen] = 0;
	set_last_cmdline(c);
	while (*c == ' ' || *c == ':') c++;
	if (!strcmp(c, "w") || !strcmp(c, "w!")) ex_write(c[1] == '!');
	else if (!strcmp(c, "q") || !strcmp(c, "q!")) core_close(E, c[1] == '!');
	else if (!strcmp(c, "wq") || !strcmp(c, "wq!")) { if (!ex_write(c[2] == '!')) core_close(E, 0); }
	else if (!strcmp(c, "x")) { if (!core_modified(B) || !ex_write(0)) core_close(E, 0); }
	/* Davy's :Format (conform.nvim): the whole buffer */
	else if (!strcmp(c, "Format")) { if (is_sql()) sql_format(0, text_len(B->text)); }
	else if (*c) core_msg(E, "E492: Not an editor command: %s", c);
}

static void cmdline_key(int k)
{
	if (k == KEY_ESC || k == KEY_CTRL_C) { E->mode = MODE_NORMAL; restart_insert(); return; }
	if (k == KEY_ENTER || k == '\n') { E->mode = MODE_NORMAL; core_msg(E, ""); if (E->cmdtype == ':') ex(); restart_insert(); return; }
	if (k == KEY_BS || k == 8) {
		if (!E->cmdlen) { E->mode = MODE_NORMAL; restart_insert(); return; }
		E->cmdlen = E->cmdpos = uni_gprev((unsigned char *)E->cmd, E->cmdlen, E->cmdlen);
		return;
	}
	if (k < 0x20 || k >= KEY_UP || E->cmdlen + 4 >= sizeof E->cmd) return;
	char b[4];
	long n = key_to_bytes(k, b);
	memcpy(E->cmd + E->cmdlen, b, (size_t)n);
	E->cmdpos = E->cmdlen += (size_t)n;
}
