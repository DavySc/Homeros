/* Snippets (snip.h).  The session copies Neovim 0.12's
   runtime/lua/vim/snippet.lua: its stops are extmarks that every change
   moves with the gravities Tabstop:set_gravity() gives (snip_edit, as
   marktree_splice()), the jumps (Session:get_dest_index), the selection
   (select_tabstop), the mirrors (the TextChanged autocmds) and when it ends
   (the CursorMoved and ModeChanged autocmds).  The body grammar is VS
   Code's, the one Davy's catalogue is written in: Neovim's
   (lsp/_snippet_grammar.lua) refuses a placeholder holding text and a
   stop, and a '$' that starts nothing, which VS Code takes.  The popup
   follows Davy's nvim-cmp setup (my-nvim lua/config/core/completion.lua). */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "core.h"
#include "loop.h"
#include "file.h"
#include "match.h"
#include "snip.h"
#include "sqlfmt.h"
#include "str.h"
#include "tsql.h"
#include "uni.h"

/* head: the catalogue's "## " heading above it, the snippet's group */
typedef struct { char *name, *desc, *body; const char *head; } Snip;
static char *cat;              /* the catalogue's bytes, cut into Snips in place */
static Snip *sn;
static PumItem *items;         /* the popup's, best first */
static int nsn, loaded;
static Match m;

/* An extmark: a position and its gravity (right: text inserted at it goes
   before it). */
typedef struct { size_t at; int right; } Mark_;
/* A tab stop: idx 0 is the end; place is its node's number among the
   body's top-level nodes (M.expand()'s placement). */
typedef struct { int idx, place; Mark_ a, z; } Stop;
static struct {
	Editor *e;
	Buf *b;                    /* NULL: no snippet active */
	int nr;                    /* b's number: a buffer freed and made again at b is another */
	Stop *s;
	int n, cur;                /* cur: the active stop's slot */
	Mark_ ra, rz;              /* the whole snippet */
	size_t line, col;          /* the cursor CursorMoved last saw */
	/* b's changedtick, and its values when TextChangedI and TextChanged
	   last ran (b_last_changedtick_i, b_last_changedtick) */
	unsigned tick, ti, tn;
	int syncing;               /* sync() is writing a mirror */
} S;
/* Visual mode is Select mode: a placeholder was selected, and Select mode
   outlives the snippet, as Vim's mode does */
static int sel;

/* Read once, at the first key in a .sql buffer: an edit to the catalogue
   shows after a restart (step 1; reloading on a change can come later) */
static void load(void)
{
	char f[4096];
	size_t n;
	FileId id;
	loaded = 1;
	file_stdpath(f, sizeof f, 2, "/snippets/sql.md");
	if (file_load(f, &cat, &n, &id)) return;
	file_eol(cat, &n);
	char *c = realloc(cat, n + 1);
	if (!c) { free(cat); cat = NULL; return; }
	cat = c;
	cat[n] = 0;
	int k = 0;
	for (char *p = cat; (p = strstr(p, "```sql\n")); p++) k++;
	if (!(sn = malloc(k * sizeof *sn))) return;
	const char *head = "";
	for (char *p = cat, *q; (q = strstr(p, "```sql\n-- ")); ) {
		for (char *h = p; (h = strstr(h, "\n## ")) && h < q; h++) head = h + 4;
		Snip *s = &sn[nsn];
		s->head = head;
		s->name = p = q + 10;
		char *nl = strchr(p, '\n'), *end = nl ? strstr(nl, "\n```") : NULL;
		if (!end) break;
		char *colon = strstr(p, " : ");
		if (colon > nl) colon = NULL;
		s->desc = colon ? colon + 3 : nl;
		*(colon ? colon : nl) = 0;
		*nl = 0;
		s->body = nl + 1;
		*end = 0;   /* the body without its last newline, as vim.snippet takes one */
		p = end + 4;
		nsn++;
	}
}

static int is_sql(const Buf *b)
{
	size_t n = b->path ? strlen(b->path) : 0;
	return n > 4 && !memcmp(b->path + n - 4, ".sql", 4);
}

static size_t cursor(Editor *e) { return core_line_start(e, e->win->line) + e->win->col; }

static void set_cursor(Editor *e, size_t pos)
{
	e->win->line = text_line_of(e->buf->text, pos);
	e->win->col = pos - core_line_start(e, e->win->line);
}

static void stop(void)
{
	free(S.s);
	memset(&S, 0, sizeof S);
}

/* marktree_splice(): bytes [pos, pos + ndel) became nins bytes.  A mark in
   that range, or at either edge of it, ends at its start, or after the new
   text with right gravity. */
static void splice(Mark_ *k, size_t pos, size_t ndel, size_t nins)
{
	if (k->at > pos + ndel) k->at = k->at - ndel + nins;
	else if (k->at >= pos) k->at = k->right ? pos + nins : pos;
}

/* The file's index: its names (the facts up to I.nf, in the pool up to
   I.np) and its statements (I.u, sqlfmt_units as start and end marks),
   from one walk over it.  It is made when a word starts in a buffer
   without one, and again when typing pauses (HOLD ms after the last
   change, Neovim's CursorHold with a shorter 'updatetime'), so the walk
   (26 to 43 ms at 700 KB) stays off the keys: keys while typing come
   faster than HOLD, and one after a pause rarely lands in the walk.
   Until then snip_edit moves its statements as extmarks, and a word's
   statement is walked again when the word starts. */
#define HOLD 1000
static struct { const Buf *b; int nr, stale; size_t len, np; int nf; Str u; } I;
static Editor *E;   /* snip_key's, for the timer (one, as main.c's) */
static void idle(void *ctx);
#define NU ((size_t)(I.u.n / sizeof(Mark_[2])))

void snip_edit(const Buf *b, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	if (b == I.b) {   /* the index's statements move as extmarks */
		Mark_ (*u)[2] = (Mark_ (*)[2])I.u.s;
		for (size_t i = 0; i < NU; i++) splice(&u[i][0], pos, ndel, nins), splice(&u[i][1], pos, ndel, nins);
		I.len += nins - ndel;
		I.stale = 1;
	}
	if (E && is_sql(b)) loop_timer(idle, NULL, b == I.b ? HOLD : 0);   /* a buffer without it: at once, off the key */
	if (!S.b || b != S.b) return;
	/* a join reaches the core as its lines rewritten (keys' do_join());
	   Neovim splices extmarks over the line break it takes out alone.  A
	   mirror is spliced whole, as nvim_buf_set_text() does. */
	size_t k = 0, d = ndel, n = nins;
	char c;
	while (d && n && text_read(t, pos + k, &c, 1) == 1 && c == ins[k]) k++, d--, n--;
	while (d && n && text_read(t, pos + k + d - 1, &c, 1) == 1 && c == ins[k + n - 1]) d--, n--;
	if (!S.syncing && !n) pos += k, ndel = d, nins = 0;
	/* Enter at the end of a line reaches the core as a line put in after
	   its break (keys' open_line()); Neovim's open_line() splices extmarks
	   at the break itself, the same text one byte earlier */
	if (!ndel && nins && ins[nins - 1] == '\n' && pos && text_read(t, pos - 1, &c, 1) == 1 && c == '\n') pos--;
	for (int j = 0; j < S.n; j++) splice(&S.s[j].a, pos, ndel, nins), splice(&S.s[j].z, pos, ndel, nins);
	splice(&S.ra, pos, ndel, nins);
	splice(&S.rz, pos, ndel, nins);
	S.tick++;
	/* changed_common(): a change over the cursor's line makes the next
	   check run CursorMoved even if the cursor is where it last ran */
	size_t l = text_line_of(t, pos), le = text_line_of(t, pos + ndel) + 1, cl = S.e->win->line;
	long x = -(long)(le - 1 - l);
	for (size_t i = 0; i < nins; i++) x += ins[i] == '\n';
	if (l <= cl && le + (size_t)(x < 0 ? -x : x) > cl) S.line = (size_t)-1;
}

/* The TextChanged autocmds: a snippet whose text is all gone ends; the
   active stop's text goes to each of its mirrors (Tabstop:set_text). */
static void sync(Editor *e)
{
	if (S.ra.at == S.rz.at || S.rz.at > text_len(e->buf->text)) { stop(); return; }
	Stop c = S.s[S.cur];
	size_t n = c.z.at - c.a.at;
	char *t = malloc(n + 1);
	if (!t) return;
	text_read(e->buf->text, c.a.at, t, n);
	for (int j = 0; j < S.n; j++) {
		Stop *o = &S.s[j];
		if (j == S.cur || o->idx != c.idx) continue;
		size_t a = o->a.at, z = o->z.at, at = cursor(e);
		S.syncing = 1;
		int r = core_edit(e, a, z - a, t, n);
		S.syncing = 0;
		if (r) break;
		/* nvim_buf_set_text()'s fix_cursor_cols(): a cursor after the text
		   moves with it, one inside it goes no further than its end */
		if (at > z) set_cursor(e, at - (z - a) + n);
		else if (at > a + n) set_cursor(e, a + n);
	}
	free(t);
}

/* Session:set_gravity(): the stops of the number jumped to grow with what
   is typed at either edge; one that a run of other stops leads up to with
   no text between stays put (lock), the rest move on (shift). */
static void set_gravity(int idx)
{
	for (int j = 0; j < S.n; j++) {
		Stop *t = &S.s[j];
		if (t->idx == idx) { t->a.right = 0; t->z.right = 1; continue; }
		int lock = 0;
		for (int p = t->place + 1, more = 1; more && !lock; p++) {
			more = 0;
			for (int i = 0; i < S.n; i++)
				if (S.s[i].place == p) lock |= S.s[i].idx == idx, more = 1;
		}
		t->a.right = t->z.right = !lock;
	}
}

/* select_tabstop(): an empty stop, or the end, is typed at in Insert mode;
   a placeholder is selected, the cursor at its start. */
static void select_stop(Editor *e, Stop *t)
{
	int ins = e->mode == MODE_INSERT;
	/* the cursor moves before the keys run (they are typeahead): Esc leaves
	   Insert mode on the stop's line, and an autoindent left behind stays */
	set_cursor(e, t->a.at);
	if (t->idx == 0 || t->a.at == t->z.at) {
		if (!ins) {
			keys_feed(e, KEY_ESC, 0);
			set_cursor(e, t->a.at);
			size_t len = core_line_len(e, e->win->line);
			int append = len && e->win->col >= len;   /* startinsert! at the end of the line */
			if (append) e->win->col = len - 1;
			keys_feed(e, append ? 'a' : 'i', 0);
		}
		return;
	}
	if (ins || e->ks.visual) keys_feed(e, KEY_ESC, 0);
	set_cursor(e, t->a.at);
	keys_feed(e, 'v', 0);
	/* move_cursor_to() the end: its last character, or the start of its
	   line when it ends at one (that line's first character is selected) */
	unsigned char b[4];
	size_t k = t->z.at - t->a.at < 4 ? t->z.at - t->a.at : 4;
	text_read(e->buf->text, t->z.at - k, (char *)b, k);
	set_cursor(e, b[k - 1] == '\n' ? t->z.at : t->z.at - k + uni_prev(b, k, k));
	keys_feed(e, 'o', 0);
	sel = 1;
}

/* M.jump() from stop number cur: 1 when it moved, 0 when there is nowhere
   to go that way. */
static int jump(Editor *e, int dir, int cur)
{
	int dest = -1;
	for (int j = 0; j < S.n; j++) {
		int i = S.s[j].idx;
		if (i > 0 && (dir > 0 ? i > cur && (dest < 0 || i < dest) : i < cur && i > dest)) dest = i;
	}
	if (dest < 0 && dir < 0) return 0;
	if (dest < 0) dest = 0;
	int best = -1;
	for (int j = 0; j < S.n; j++)
		if (S.s[j].idx == dest && (best < 0 || S.s[j].a.at < S.s[best].a.at)) best = j;
	S.cur = best;
	sel = 0;
	set_gravity(dest);
	/* the TextChanged(I) that follows select_tabstop()'s keys, run before
	   them: they are typeahead, which never syncs undo (gotchars()), so its
	   mirror writes join the open undo step, as keys_feed() would not */
	Stop *t = &S.s[best];
	unsigned *seen = !dest || t->a.at == t->z.at ? &S.ti : &S.tn;
	if (*seen != S.tick) sync(e), *seen = S.tick;
	if (!S.b) return 1;
	select_stop(e, &S.s[best]);
	if (dest == 0) stop();
	else S.line = e->win->line, S.col = e->win->col;
	return 1;
}

/* After a key typed in mode ins (1 Insert), the autocmds in Neovim's
   order: CursorMoved in Insert and select mode ends the snippet when the
   cursor left it or is on no stop but the end; TextChangedI, or TextChanged
   outside Insert mode, sets the mirrors when the text changed since it last
   ran; ModeChanged to Normal mode ends the snippet.  None runs while keys
   are pending (char_avail()). */
static void after(Editor *e, int ins)
{
	int now = e->mode == MODE_INSERT, vis = sel && e->ks.visual;
	if (ins && !now && S.ti == S.tick) S.tn = S.tick;   /* the end of edit() */
	if ((now || vis) && (e->win->line != S.line || e->win->col != S.col)) {
		size_t at = cursor(e);
		int on = 0;
		S.line = e->win->line, S.col = e->win->col;
		for (int j = 0; j < S.n; j++) on |= S.s[j].idx > 0 && S.s[j].a.at <= at && at <= S.s[j].z.at;
		if (at < S.ra.at || at > S.rz.at || !on) { stop(); return; }
	}
	unsigned *seen = now ? &S.ti : &S.tn;
	if (*seen != S.tick) sync(e);
	if (!S.b) return;
	*seen = S.tick;
	if (!now && !vis) stop();
}

/* sel: the Visual selection a surround puts at $0, its first line's
   indent base blanks wide */
typedef struct { char *s; size_t n, cap; Stop *st; int nst, cap_st, max, place; const char *sel; size_t nsel, base; } Out;

/* n bytes of s at byte at of o's text */
static int put_at(Out *o, size_t at, const char *s, size_t n)
{
	if (o->n + n > o->cap || !o->s) {
		size_t c = (o->n + n) * 2 + 16;
		char *p = realloc(o->s, c);
		if (!p) return -1;
		o->s = p;
		o->cap = c;
	}
	memmove(o->s + at + n, o->s + at, o->n - at);
	if (n) memcpy(o->s + at, s, n);
	o->n += n;
	return 0;
}

static int put(Out *o, const char *s, size_t n) { return put_at(o, o->n, s, n); }

static int add(Out *o, int idx, size_t a, int top)
{
	if (o->nst == o->cap_st) {
		int c = o->cap_st * 2 + 8;
		Stop *p = realloc(o->st, c * sizeof *p);
		if (!p) return -1;
		o->st = p;
		o->cap_st = c;
	}
	/* Tabstop.new(): right gravity at the start, left at the end; a stop
	   inside a placeholder (VS Code's, not Neovim's) is next to nothing */
	o->st[o->nst] = (Stop){ idx, top ? ++o->place : -2, { a, 1 }, { a, 0 } };
	if (idx > o->max) o->max = idx;
	return o->nst++;
}

/* The selection as stop k's text ($0, VS Code's TM_SELECTED_TEXT, SSMS's
   surround with): each line after the first loses the first line's indent
   and is indented as the line the stop is on (VS Code's adjustWhitespace) */
static int put_sel(Out *o, int k)
{
	size_t i = o->n, w = 0;
	while (i && o->s[i - 1] != '\n') i--;
	char ind[256];
	while (i + w < o->n && w < sizeof ind && memchr(" \t", o->s[i + w], 2)) ind[w] = o->s[i + w], w++;
	for (size_t j = 0; j < o->nsel; j++) {
		for (size_t b = 0; j && b < o->base && j < o->nsel && memchr(" \t", o->sel[j], 2); b++) j++;
		size_t z = j;
		while (z < o->nsel && o->sel[z] != '\n') z++;
		if (put(o, o->sel + j, z - j) || (z < o->nsel && (put(o, "\n", 1) || put(o, ind, w)))) return -1;
		j = z;
	}
	o->st[k].z.at = o->n;
	return 0;
}

static int digit(char c) { return c >= '0' && c <= '9'; }
static int alpha(char c) { return c == '_' || ((c | 32) >= 'a' && (c | 32) <= 'z'); }

/* The body from *p into o, each line after the first indented as the
   cursor line (ind) and a Tab as 4 spaces (append_to_snippet(), with
   FEATURES.md's expandtab and shiftwidth 4), up to the '}' that closes a
   placeholder when braced.  A run of text is one node, as each stop is,
   counted at the top level only, as Neovim numbers the placements.
   -1 out of memory. */
static int parse(Out *o, const char **p, const char *ind, size_t nind, int braced)
{
	int text = 0;   /* inside a text node */
	for (const char *s = *p; *s; *p = s) {
		const char *q = s + 1;
		int r = 0;
		if (braced && *s == '}') { *p = s + 1; return 0; }
		if (*s == '$' && (digit(*q) || (*q == '{' && digit(q[1])))) {
			/* $1, ${1}, ${1:placeholder} */
			const char *d = *q == '{' ? q + 1 : q;
			int idx = 0;
			while (digit(*d) && idx < 10000) idx = idx * 10 + (*d++ - '0');
			if (*q != '{' || *d == ':' || *d == '}') {
				int k = add(o, idx, o->n, !braced);
				if (k < 0) return -1;
				if (*q == '{' && *d++ == ':') {
					if (parse(o, &d, ind, nind, 1)) return -1;
					o->st[k].z.at = o->n;
				} else if (!idx && o->sel && put_sel(o, k)) return -1;
				s = d;
				text = 0;
				continue;
			}
		} else if (*s == '$' && (alpha(*q) || (*q == '{' && alpha(q[1])))) {
			/* a variable; none is known, so its name becomes a new stop
			   (Neovim and VS Code), any default dropped */
			const char *d = *q == '{' ? q + 1 : q, *nm = d;
			while (alpha(*d) || digit(*d)) d++;
			size_t nn = (size_t)(d - nm);
			for (int depth = *q == '{'; *d && depth; d++) depth += (*d == '{') - (*d == '}');
			int k = add(o, o->max + 1, o->n, !braced);
			if (k < 0 || put(o, nm, nn)) return -1;
			o->st[k].z.at = o->n;
			s = d;
			text = 0;
			continue;
		}
		if (!text) o->place += !braced, text = 1;
		if (*s == '\\' && s[1] && strchr("$}\\", s[1])) r = put(o, s + 1, 1), s += 2;
		else if (*s == '\n') r = put(o, "\n", 1) || put(o, ind, nind), s++;
		else if (*s == '\t') r = put(o, "    ", 4), s++;
		else r = put(o, s, 1), s++;
		if (r) return -1;
	}
	return 0;
}

/* the blanks that start line l (core_first_nonblank() backs up onto the
   last of a blank line's outside Insert mode) */
static size_t indent(Editor *e, size_t l)
{
	size_t n, i = 0;
	const unsigned char *s = core_line(e, l, &n);
	while (i < n && memchr(" \t", s[i], 2)) i++;
	return i;
}

/* M.expand(): the snippet replaces bytes [a, z) (the word typed, or the
   selection sel that goes to $0). */
static int expand(Editor *e, const char *body, size_t a, size_t z, const char *sel, size_t nsel)
{
	stop();
	Out o = { .sel = sel, .nsel = nsel, .base = indent(e, e->win->line) };
	const unsigned char *l = core_line(e, e->win->line, &(size_t){ 0 });
	size_t nind = o.base;
	char ind[256];
	if (nind > sizeof ind) nind = sizeof ind;
	memcpy(ind, l, nind);
	const char *p = body;
	int r = parse(&o, &p, ind, nind, 0) || put(&o, "", 0), has0 = 0;
	/* each bare stop shows its number's placeholder: the stops after it
	   move on and a stop around it grows */
	for (int j = 0; !r && j < o.nst; j++) {
		Stop *t = &o.st[j];
		has0 |= !t->idx;
		for (int i = 0; t->a.at == t->z.at && i < o.nst; i++) {
			Stop *ph = &o.st[i];
			if (ph->idx != t->idx) continue;
			size_t n = ph->z.at - ph->a.at, at = t->a.at;
			char *c = malloc(n);
			r = !c || (memcpy(c, o.s + ph->a.at, n), put_at(&o, at, c, n));
			free(c);
			for (int k = 0; k < o.nst; k++) {
				Stop *u = &o.st[k];
				int on = u->place > t->place;   /* after it in the body */
				u->z.at += u->z.at > at || (u->z.at == at && on) || k == j ? n : 0;
				u->a.at += u->a.at > at || (u->a.at == at && on) ? n : 0;
			}
		}
	}
	if (!r && !has0) {
		int k = add(&o, 0, o.n, 1);
		r = k < 0 || (sel && put_sel(&o, k));
	}
	if (r || core_edit(e, a, z - a, o.s, o.n)) { free(o.s); free(o.st); return -1; }
	free(o.s);
	for (int j = 0; j < o.nst; j++) o.st[j].a.at += a, o.st[j].z.at += a;
	S.e = e;
	S.b = e->buf;
	S.nr = e->buf->nr;
	S.s = o.st;
	S.n = o.nst;
	S.ra = (Mark_){ a, 0 };
	S.rz = (Mark_){ a + o.n, 1 };
	S.tick = 1;   /* the expansion, which neither autocmd has seen */
	set_cursor(e, a + o.n);
	jump(e, 1, 0);   /* Session.new() starts before the first stop */
	return 0;
}

/* Davy's file names, <prefix>_<group>_<name>.sql: the first word that
   names a group, as a word of the catalogue's headings ("proc" files get
   the shared helpers); NULL for none */
static const char *group(const Buf *b)
{
	static const char *const g[] = { "layout", "default", "context", "badge", "task", "trigger", "handler", "process", "proc" };
	const char *p = b->path ? b->path : "", *w;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\') p = q + 1;
	for (w = p; *w; w += *w == '_') {
		size_t n = strcspn(w, "_.");
		for (size_t i = 0; i < sizeof g / sizeof *g; i++)
			if (n == strlen(g[i]) && !strncmp(w, g[i], n)) return i == 8 ? "helpers" : g[i];
		w += n;
		if (*w == '.') break;
	}
	return NULL;
}

/* 1 when heading h, up to its '(' or line end, has the word w */
static int heading_has(const char *h, const char *w)
{
	size_t n = strlen(w);
	for (const char *p = h; *p && *p != '(' && *p != '\n'; p++)
		if ((p == h || !alpha(p[-1])) && !strncasecmp(p, w, n) && !alpha(p[n])) return 1;
	return 0;
}

/* A candidate of the popup, its word and the table it belongs to (a
   column's, an alias's; -1 none) as offsets into pool.  pri: the lower
   first, before the score: in the popup snippets after the rest (2), as
   blink.cmp's score_offset puts them after the server's items, and a
   snippet of the file's group (Davy's file names) before the others. */
typedef struct { size_t w, of; const char *kind, *menu; int pri; } Cand;
static Str pool, cands, facts;   /* facts: the file's names, as Cands */
static int *pick, *score, npick, pickcap;
static struct { Buf *b; int nr, ok; size_t line, col; } W;   /* the word cands are for, ok: a popup */
#define CAND(v, i) (((Cand *)(v).s)[i])
#define NCAND(v) ((int)((v).n / sizeof(Cand)))

static int word(int c) { return alpha((char)c) || digit((char)c) || c == '-' || c == '@' || c == '#'; }

/* n bytes of w into the pool, NUL-terminated: their offset, or -1 */
static size_t intern(const char *w, size_t n)
{
	size_t at = pool.n;
	return str_add(&pool, w, n) || str_add(&pool, "", 1) ? (size_t)-1 : at;
}

static void cand(Str *to, const char *w, size_t n, size_t of, const char *kind, const char *menu, int pri)
{
	Cand c = { intern(w, n), of, kind, menu, pri };
	if (c.w != (size_t)-1) str_add(to, (const char *)&c, sizeof c);
}

/* nvim-cmp's matcher (matcher.lua, disallow_partial_fuzzy_matching on):
   the query is runs of w's characters, each starting a word part (after a
   character that is no letter or digit, or a capital after a small one).
   The score is the pickers' (match.c). */
static int parts(const char *q, size_t n, const char *w)
{
	size_t j = 0;
	for (const char *p = w; *p && j < n; p++) {
		int start = p == w || !(alpha(p[-1]) || digit(p[-1])) || (p[-1] != '_' && (*p | 32) != *p && (p[-1] | 32) == p[-1]);
		if (!start || (*p | 32) != (q[j] | 32)) continue;
		while (*p && j < n && (*p | 32) == (q[j] | 32)) p++, j++;
		p--;
	}
	return j == n;
}

static const char *qs;           /* the query rank() sorts for */
static size_t nqs;

/* nvim-cmp's comparators (exact, score, length, order), pri before the
   score */
static int by_rank(const void *x, const void *y)
{
	int i = *(const int *)x, j = *(const int *)y;
	const Cand *a = &CAND(cands, i), *b = &CAND(cands, j);
	int ea = nqs && !strncasecmp(pool.s + a->w, qs, nqs) && !pool.s[a->w + nqs], eb = nqs && !strncasecmp(pool.s + b->w, qs, nqs) && !pool.s[b->w + nqs];
	if (ea != eb) return eb - ea;
	if (a->pri != b->pri) return a->pri - b->pri;
	if (score[i] != score[j]) return score[j] > score[i] ? 1 : -1;
	size_t la = strlen(pool.s + a->w), lb = strlen(pool.s + b->w);
	return la != lb ? la < lb ? -1 : 1 : i - j;
}

/* items: the cands that match the n bytes of q (n 0: all), best first;
   cmp: by nvim-cmp's word parts (the popup), else as snacks' picker */
static int rank(const char *q, size_t nq, int cmp)
{
	int n = NCAND(cands);
	if (n > pickcap) {
		int *p = realloc(pick, n * (sizeof *pick + sizeof *score + sizeof *items));
		if (!p) return 0;
		pick = p, score = p + n, items = (PumItem *)(score + n), pickcap = n;
	}
	char low[MATCH_MAX];
	if (cmp && nq < sizeof low) {   /* nvim-cmp ignores case, snacks' smartcase would not */
		for (size_t i = 0; i < nq; i++) low[i] = (char)(q[i] >= 'A' && q[i] <= 'Z' ? q[i] + 32 : q[i]);
		q = low;
	}
	if (nq && match_init(&m, q, nq)) return 0;
	npick = 0;
	for (int i = 0; i < n; i++) {
		const char *w = pool.s + CAND(cands, i).w;
		score[i] = nq ? !cmp || parts(q, nq, w) ? match_score(&m, w, strlen(w), NULL, 0) : 0 : 1;
		if (score[i] > 0) pick[npick++] = i;
	}
	qs = q, nqs = cmp ? nq : 0;   /* the picker keeps snip_get's order */
	if (npick) qsort(pick, npick, sizeof *pick, by_rank);
	for (int i = 0; i < npick; i++) {
		const Cand *c = &CAND(cands, pick[i]);
		items[i] = (PumItem){ pool.s + c->w, c->kind, c->menu };
	}
	return npick;
}

/* the catalogue's snippets as cands, the file's group's at pri - 1 and the
   rest at pri; task snippets only in a task file */
static void snippets(Editor *e, int pri)
{
	const char *g = group(e->buf);
	if (!loaded) load();
	for (int i = 0; i < nsn; i++)
		if (!heading_has(sn[i].head, "task") || (g && !strcmp(g, "task")))
			cand(&cands, sn[i].name, strlen(sn[i].name), (size_t)-1, "Snippet", sn[i].desc, pri - (g && heading_has(sn[i].head, g)));
}

static const char *body_of(const char *name)
{
	if (!loaded) load();
	for (int i = 0; i < nsn; i++) if (!strcmp(sn[i].name, name)) return sn[i].body;
	return NULL;
}

const char *snip_get(Editor *e, const char *q, size_t nq, int i, const char **desc, const char **body)
{
	static int n;
	if (!i) {
		W.b = NULL, pool.n = I.np, cands.n = 0;
		snippets(e, 0);
		n = rank(q, nq, 0);
	}
	if (i >= n) return NULL;
	*desc = items[i].menu;
	*body = body_of(items[i].word);
	return items[i].word;
}

/* ---- the file's names, by the formatter's statements (sqlfmt_units) ---- */

/* a token of the walk: kind, what it is to the walk (f), start, and its
   text without [] or "" */
enum { NAME = 1, FROM = 2, JOIN = 4, AS = 8, TABLE = 16, TABLES = 32, OR_WORD = 64 };
typedef struct { int k, f; size_t s; char b[128]; } Tok;
static int sym(const Tok *t, char c) { return t->k == TQ_PUNCT && *t->b == c; }
static int is_of(const Cand *c, const char *w) { return c->of != (size_t)-1 && !strcasecmp(pool.s + c->of, w); }

/* facts by name, then kind and table, any case: sorted, a run of equal
   ones is one fact */
static int by_fact(const void *x, const void *y)
{
	const Cand *a = x, *b = y;
	int c = strcasecmp(pool.s + a->w, pool.s + b->w);
	if (!c) c = strcmp(a->kind, b->kind);
	return c ? c : strcasecmp(a->of == (size_t)-1 ? "" : pool.s + a->of, b->of == (size_t)-1 ? "" : pool.s + b->of);
}
static void sort_facts(int from)
{
	int n = NCAND(facts) - from, k = 0;
	if (n < 1) return;
	Cand *f = (Cand *)facts.s + from;
	qsort(f, (size_t)n, sizeof *f, by_fact);
	for (int i = 0; i < n; i++) if (!k || by_fact(&f[k - 1], &f[i])) f[k++] = f[i];
	facts.n = (size_t)(from + k) * sizeof *f;
}
static void fact(const char *w0, const char *of0, const char *kind)
{
	char w[128], of[128];   /* w0 and of0 may be in the pool, which intern moves */
	strncpy(w, w0, sizeof w - 1)[sizeof w - 1] = 0, strncpy(of, of0, sizeof of - 1)[sizeof of - 1] = 0;
	size_t o = *of ? intern(of, strlen(of)) : (size_t)-1;
	if (o != (size_t)-1 || !*of) cand(&facts, w, strlen(w), o, kind, NULL, 0);
}

/* a statement's end: each Q.name of it (pairs: name, then Q) becomes a
   column of Q's table (Q an alias in refs) or of Q; its refs go to here */
static void flush(Str *refs, Str *pairs, Str *here)
{
	if (here && refs->n) str_add(here, refs->s, refs->n);
	for (int i = 0; i < NCAND(*pairs); i++) {
		const char *q = pool.s + CAND(*pairs, i).of;
		for (int j = 0; j < NCAND(*refs); j++)
			if (!strcasecmp(pool.s + CAND(*refs, j).w, q)) q = pool.s + CAND(*refs, j).of;
		fact(pool.s + CAND(*pairs, i).w, q, "Column");
	}
	refs->n = pairs->n = 0;
}

/* One walk over the tokens of [p, end) of t, statement by statement (u):
   a table after FROM, JOIN, INTO, UPDATE, DELETE, MERGE or TABLE or a FROM
   list's comma (schema.table: the table, of the schema), or a CTE (x AS
   (); its alias after it (or AS alias); a column list after it (CREATE
   TABLE, DECLARE @t TABLE, INSERT INTO t (...)) gives columns, as Q.name
   does; @ and # names.  The word at [a, z) is skipped and ctx gets the two
   tokens before it; the last statement's tables and aliases go to here.
   0: a is in a comment or a string. */
static int walk(const Text *t, size_t p, size_t end, const Mark_ (*u)[2], size_t nu, size_t a, size_t z, Tok ctx[2], Str *here)
{
	/* the words the walk looks for, by tsql_word's id; the rest of
	   tables[] are the words a table follows */
	static const char *const w[] = { "from", "join", "as", "table", "into", "update", "delete", "merge" };
	static size_t id[8];
	if (!id[0])
		for (int i = 0; i < 8; i++) tsql_word(w[i], strlen(w[i]), &id[i]);
	size_t ui = 0, n, tab = (size_t)-1, d;
	Tok r[3] = { 0 };   /* the last three tokens (a ring) */
	Str refs = { 0 }, pairs = { 0 };
	int k, from = 0, list = 0, got = 0, i = 0;
	TsqlRd rd = { .t = t };
	for (; p < end; p += n) {
		size_t e = tsql_token(&rd, p, &k);
		if (e <= p) break;
		n = e - p;
		if ((k == TQ_COMMENT || k == TQ_STRING) && p < a && a < e) break;
		if (k == TQ_SPACE || k == TQ_COMMENT || (p >= a && p < z)) continue;
		if (p >= a && !got) got = 1, ctx[0] = r[i], ctx[1] = r[(i + 2) % 3];
		for (; ui < nu && u[ui][1].at <= p; ui++) flush(&refs, &pairs, NULL);
		i = (i + 1) % 3;
		Tok *h0 = &r[i], *h1 = &r[(i + 2) % 3], *h2 = &r[(i + 1) % 3];
		char *b = h0->b;
		d = n < 127 ? n : 127;
		text_read(t, p, b, d);
		b[d] = 0;
		/* [x] and "x" are x, unless x needs them */
		int plain = d >= 2 && (*b == '[' || *b == '"');
		for (size_t j = 1; plain && j + 1 < d; j++) plain = alpha(b[j]) || digit(b[j]) || b[j] == '_';
		if (plain) memmove(b, b + 1, d -= 2), b[d] = 0;
		size_t wid = 0;
		int c = k == TQ_WORD ? tsql_word(b, d, &wid) : 0;
		h0->k = k, h0->s = p, h0->f = k == TQ_IDENT || k == TQ_VAR || (k == TQ_WORD && !(c & TQ_RECASE));
		for (int j = 0; wid && j < 8; j++) if (wid == id[j]) h0->f |= j < 4 ? FROM << j : j < 6 ? TABLES : TABLES | OR_WORD;
		if (h0->f & TABLE) h0->f |= TABLES;
		if (k == TQ_VAR && b[1] != '@') fact(b, "", *b == '@' ? "Variable" : "Table");
		if (list) {   /* a column list: a name after its '(' or a ',' */
			list += sym(h0, '(') - sym(h0, ')');
			if (list == 1 && (h0->f & NAME) && (sym(h1, '(') || sym(h1, ','))) fact(b, pool.s + tab, "Column");
			continue;
		}
		if (h0->f & FROM) from = 1;
		else if (k == TQ_WORD && !(h0->f & (NAME | JOIN | AS))) from = 0;
		int isname = h0->f & NAME, attab = tab != (size_t)-1;
		if (isname && attab && sym(h1, '.') && !strcasecmp(h2->b, pool.s + tab)) {
			/* schema.table: the table before was its schema */
			if (facts.n && !strcasecmp(pool.s + CAND(facts, NCAND(facts) - 1).w, h2->b)) facts.n -= sizeof(Cand);
			refs.n -= sizeof(Cand);
			fact(b, h2->b, "Table");
			tab = intern(b, d);
			cand(&refs, b, d, tab, "Table", NULL, 0);
		} else if (isname && ((h1->f & (TABLES | FROM | JOIN)) || (from && sym(h1, ',')))) {
			fact(b, "", "Table");   /* a table */
			tab = intern(b, d);
			if (tab != (size_t)-1) cand(&refs, b, d, tab, "Table", NULL, 0);
		} else if (isname && attab && (!strcasecmp(h1->b, pool.s + tab) || ((h1->f & AS) && !strcasecmp(h2->b, pool.s + tab)))) {
			cand(&refs, b, d, tab, "Alias", NULL, 0);   /* its alias */
		} else if (sym(h0, '(') && (h1->f & AS) && (h2->f & NAME)) {
			fact(h2->b, "", "Table");   /* a CTE */
		} else if (sym(h0, '(') && attab && ((h1->f & TABLE) || !strcasecmp(h1->b, pool.s + tab))) {
			list = 1;
		} else if ((h0->f & TABLE) && h1->k == TQ_VAR) {
			fact(h1->b, "", "Table");   /* DECLARE @t TABLE */
			tab = intern(h1->b, strlen(h1->b));
		} else if (isname && sym(h1, '.') && (h2->f & NAME)) {
			size_t q = intern(h2->b, strlen(h2->b));
			if (q != (size_t)-1) cand(&pairs, b, d, q, "Column", NULL, 0);
		}
		if (!isname && !sym(h0, '.') && !(h0->f & (AS | TABLE)) && !sym(h0, '(')) tab = (size_t)-1;
	}
	if (!got) ctx[0] = r[i], ctx[1] = r[(i + 2) % 3];
	flush(&refs, &pairs, here);
	str_free(&refs), str_free(&pairs);
	return p >= end;
}


static void scan(Editor *e, size_t a, size_t z)
{
	const Text *t = e->buf->text;
	size_t (*u)[2], nu = sqlfmt_units(t, &u);
	Tok ctx[2];
	pool.n = facts.n = I.u.n = I.np = 0, I.nf = 0, I.b = NULL;
	for (size_t i = 0; i < nu; i++) {
		Mark_ m[2] = { { u[i][0], 0 }, { u[i][1], 1 } };
		if (str_add(&I.u, (const char *)m, sizeof m)) { I.u.n = 0; return; }
	}
	walk(t, 0, text_len(t), (const Mark_ (*)[2])I.u.s, NU, a, z, ctx, NULL);
	sort_facts(0);
	I.b = e->buf, I.nr = e->buf->nr, I.len = text_len(t), I.np = pool.n, I.nf = NCAND(facts), I.stale = 0;
}

static void idle(void *ctx)
{
	Editor *e = E;
	(void)ctx;
	if (e->pum && e->pum == items) loop_timer(idle, NULL, HOLD);   /* the popup's words are in the pool */
	else if (is_sql(e->buf) && (I.stale || I.b != e->buf || I.nr != e->buf->nr || I.len != text_len(e->buf->text)))
		scan(e, (size_t)-1, (size_t)-1), W.b = NULL;
}

/* The cands for the word at [a, z) of e's buffer, by where it is in its
   statement (SSMS's IntelliSense): after Q. the columns of Q's table, or
   the tables of schema Q; after FROM, JOIN, INTO, UPDATE, DELETE, MERGE or
   TABLE the file's tables; @ the variables; elsewhere T-SQL's words, the
   statement's tables and aliases and their columns, and the snippets after
   them.  The file's names come from its index, the statement's from a walk
   over it.  0: no popup (in a comment or a string). */
static int collect(Editor *e, size_t a, size_t z)
{
	const Text *t = e->buf->text;
	if (I.b != e->buf || I.nr != e->buf->nr || I.len != text_len(t)) scan(e, a, z);
	pool.n = I.np, facts.n = (size_t)I.nf * sizeof(Cand), cands.n = 0;
	/* the word's statement, or the gap between two */
	const Mark_ (*u)[2] = (const Mark_ (*)[2])I.u.s;
	size_t j = 0, nu = NU;
	while (j < nu && u[j][0].at <= a) j++;
	Mark_ s[1][2] = { { { 0, 0 }, { text_len(t), 1 } } };
	if (j && a <= u[j - 1][1].at) s[0][0] = u[j - 1][0], s[0][1] = u[j - 1][1];
	else {
		if (j) s[0][0].at = u[j - 1][1].at;
		if (j < nu) s[0][1].at = u[j][0].at;
	}
	Tok ctx[2] = { 0 };
	Str here = { 0 };
	if (!walk(t, s[0][0].at, s[0][1].at, s, 1, a, z, ctx, &here)) { str_free(&here); return 0; }
	sort_facts(I.nf);
	char c0 = 0;
	text_read(t, a, &c0, a < z);
	int dot = sym(&ctx[0], '.') && ctx[0].s + 1 == a, at = c0 == '@', tabs = !dot && (ctx[0].f & (TABLES | FROM | JOIN));
	int words = !dot && (!tabs || (ctx[0].f & OR_WORD));   /* DELETE FROM, MERGE INTO */
	const char *q = ctx[1].b;   /* the qualifier */
	for (int i = 0; dot && i < NCAND(here); i++)
		if (!strcasecmp(pool.s + CAND(here, i).w, ctx[1].b)) q = pool.s + CAND(here, i).of;
	/* the index's facts and the statement's, merged by name, a word of a
	   kind once */
	const Cand *last = NULL;
	for (int x = 0, y = I.nf, nf = NCAND(facts); x < I.nf || y < nf;) {
		const Cand *c = &CAND(facts, y == nf || (x < I.nf && by_fact(&CAND(facts, x), &CAND(facts, y)) <= 0) ? x++ : y++);
		int scope = 0;
		for (int i = 0; i < NCAND(here); i++) scope |= is_of(c, pool.s + CAND(here, i).of);
		if (!(dot ? (c->kind[0] == 'C' || c->kind[0] == 'T') && is_of(c, q) : tabs ? c->kind[0] == 'T' : at ? c->kind[0] == 'V' : scope)) continue;
		if (last && !strcmp(last->kind, c->kind) && !strcasecmp(pool.s + last->w, pool.s + c->w)) continue;
		str_add(&cands, (const char *)c, sizeof *c);
		last = c;
	}
	if (words) {
		if (!tabs && here.n) str_add(&cands, here.s, here.n);
		for (size_t i = 1; i <= tsql_nwords; i++) {
			const char *kwd = tsql_name(i);
			int c = tsql_word(kwd, strlen(kwd), NULL);
			if (c && (*kwd == '@') == at) cand(&cands, kwd, strlen(kwd), (size_t)-1, c & TQ_FUNCTION ? "Function" : "Keyword", NULL, at);
		}
		if (!at && !tabs) snippets(e, 2);
	}
	str_free(&here);
	return 1;
}

/* The popup for the word before the cursor (cmp's keyword pattern
   \h\w*\%(-\w*\)*, and @ and # names), or after a dot; it closes when there
   is none or nothing matches.  The cands are collected when the word
   starts, and filtered as it grows (DESIGN.md: lists filtered locally). */
static void popup(Editor *e)
{
	size_t len, col = e->win->col, a = col;
	const unsigned char *l = core_line(e, e->win->line, &len);
	while (a && word(l[a - 1])) a--;
	while (a < col && !alpha((char)l[a]) && l[a] != '@' && l[a] != '#') a++;
	int n = 0;
	if (a < col || (a && l[a - 1] == '.')) {
		size_t ls = core_line_start(e, e->win->line);
		if (W.b != e->buf || W.nr != e->buf->nr || W.line != e->win->line || W.col != a)
			W.ok = collect(e, ls + a, ls + col), W.b = e->buf, W.nr = e->buf->nr, W.line = e->win->line, W.col = a;
		if (W.ok) n = rank((const char *)l + a, col - a, 1);
	} else W.b = NULL;
	core_pum(e, items, n, -1, a);
}

int snip_insert(Editor *e, const char *name)
{
	const char *body = body_of(name);
	size_t a = cursor(e), z = a, len;
	if (!body) { core_msg(e, "no snippet %s", name); return -1; }
	if (e->ks.visual) {
		/* the Visual area: characters from one end to the other, whole,
		   or the lines (also for a block) from the first one's indent to
		   the last one's break; never the text's last break, as v$c */
		size_t l1 = e->ks.vline, c1 = e->ks.vcol, l2 = e->win->line, c2 = e->win->col;
		if (l1 > l2 || (l1 == l2 && c1 > c2)) l1 = l2, c1 = c2, l2 = e->ks.vline, c2 = e->ks.vcol;
		const unsigned char *l = core_line(e, l2, &len);
		a = core_line_start(e, l1) + c1;
		z = core_line_start(e, l2) + (c2 < len ? c2 + uni_glen(l, len, c2) : len + 1);
		if (e->ks.visual != 'v') a = core_line_start(e, l1) + indent(e, l1), z = core_line_start(e, l2) + len;
		if (z >= text_len(e->buf->text)) z = text_len(e->buf->text) - 1;
		keys_feed(e, KEY_ESC, 0);
	}
	char *sel = malloc(z - a + 1);
	if (!sel) return -1;
	text_read(e->buf->text, a, sel, z - a);
	set_cursor(e, a);
	int r = expand(e, body, a, z, a < z ? sel : NULL, z - a);
	free(sel);
	if (!r && e->mode == MODE_INSERT) after(e, 1);   /* as snip_key after a key */
	return r;
}

static void close_pum(Editor *e)
{
	if (e->pum == items) core_pum(e, NULL, 0, -1, 0);
	W.b = NULL;
}

/* A picked item replaces the word: a snippet expands, a T-SQL word comes
   in capitals when the word typed has capitals only */
static int accept(Editor *e, int s)
{
	Cand c = CAND(cands, pick[s]);
	size_t a = core_line_start(e, e->win->line) + e->pumcol, z = cursor(e), n = strlen(pool.s + c.w);
	char *w = pool.s + c.w, t[MATCH_MAX];
	int big = 0, small = 0;
	close_pum(e);
	if (!strcmp(c.kind, "Snippet")) return expand(e, body_of(w), a, z, NULL, 0);
	for (size_t i = 0, m = text_read(e->buf->text, a, t, z - a < sizeof t ? z - a : sizeof t); i < m; i++)
		big |= t[i] >= 'A' && t[i] <= 'Z', small |= t[i] >= 'a' && t[i] <= 'z';
	if (big && !small && (*c.kind == 'K' || *c.kind == 'F'))
		for (char *p = w; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
	if (core_edit(e, a, z - a, w, n)) return -1;
	set_cursor(e, a + n);
	return 0;
}

/* The popup's keys (cmp's preset.insert and Davy's Tab, S-Tab and Enter):
   1 taken, 0 not a popup key. */
static int pum_key(Editor *e, int k)
{
	int n = e->npum, s = e->pumsel;
	switch (k) {
	case 14: case KEY_DOWN: case KEY_TAB: core_pum(e, items, n, s + 1 < n ? s + 1 : -1, e->pumcol); return 1;
	case 16: case KEY_UP: case KEY_S_TAB: core_pum(e, items, n, s < 0 ? n - 1 : s - 1, e->pumcol); return 1;
	case 5: close_pum(e); return 1;                          /* CTRL-E */
	case 25: if (s < 0) s = 0; break;                          /* CTRL-Y */
	case KEY_ENTER: if (s >= 0) break; close_pum(e); return 0;   /* only a picked item */
	default: return 0;
	}
	accept(e, s);
	return 1;
}

int snip_key(Editor *e, int k, int pending)
{
	E = e;
	if ((I.b != e->buf || I.nr != e->buf->nr) && is_sql(e->buf)) loop_timer(idle, NULL, 0);   /* index it before a word starts */
	if (S.b && (S.b != e->buf || S.nr != e->buf->nr)) stop();   /* BufLeave */
	/* in a snippet Tab and S-Tab jump, or are themselves, popup or not
	   (blink.cmp's default keys, snippet_forward and fallback): a word
	   typed into a placeholder opens the popup */
	int tab = S.b && (k == KEY_TAB || k == KEY_S_TAB);
	if (tab) close_pum(e);
	if (tab && jump(e, k == KEY_TAB ? 1 : -1, S.s[S.cur].idx)) return 1;
	if (e->pum && e->pum == items && pum_key(e, k)) return 1;
	int ins = e->mode == MODE_INSERT;
	if (sel && e->ks.visual) {
		/* select mode: a character or Enter replaces the selection,
		   Backspace deletes it, Esc leaves it; other keys act as in Visual
		   mode (Vim's visual.txt, Select mode); <S-Tab> and CTRL-W do nothing
		   there in Neovim (measured: CTRL-W then x, v, a, CTRL-W, Del, Tab) */
		if (k == KEY_S_TAB || k == 23) return 1;
		int del = k == KEY_BS || k == KEY_DEL, typed = k == KEY_ENTER || (k >= ' ' && k != 127 && k < 0x110000);
		if (k == KEY_ESC || del || typed) {
			sel = 0;
			keys_feed(e, k == KEY_ESC ? k : del ? 'd' : 'c', 0);
			if (!typed) { if (S.b) after(e, 0); return 1; }
		}
	}
	if (!S.b && (e->mode != MODE_INSERT || !is_sql(e->buf))) return 0;
	if (!loaded) load();
	keys_feed(e, k, pending);
	if (S.b && !pending) after(e, ins);
	if (e->mode == MODE_INSERT && !pending && is_sql(e->buf) && (word(k) || k == '.' || k == KEY_BS)) popup(e);
	else close_pum(e);
	return 1;
}
