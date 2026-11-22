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
#include "file.h"
#include "match.h"
#include "snip.h"
#include "uni.h"

/* head: the catalogue's "## " heading above it, the snippet's group */
typedef struct { char *name, *desc, *body; const char *head; } Snip;
static char *cat;              /* the catalogue's bytes, cut into Snips in place */
static Snip *sn;
static PumItem *items;         /* the popup's, in score order */
static int *score, nsn, loaded;
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
	/* one block: Snips, then the popup's items and scores */
	if (!(sn = malloc(k * (sizeof *sn + sizeof *items + sizeof *score)))) return;
	items = (PumItem *)(sn + k);
	score = (int *)(items + k);
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

void snip_edit(const Buf *b, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
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

/* M.expand(): the snippet replaces bytes [a, z) (the word typed, or the
   selection sel that goes to $0). */
static int expand(Editor *e, const char *body, size_t a, size_t z, const char *sel, size_t nsel)
{
	stop();
	Out o = { .sel = sel, .nsel = nsel };
	size_t len;
	const unsigned char *l = core_line(e, e->win->line, &len);
	while (o.base < len && memchr(" \t", l[o.base], 2)) o.base++;
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

static int word(int c) { return alpha((char)c) || digit((char)c) || c == '-'; }

/* items: the snippets for e's buffer that match the n bytes of q (q NULL:
   all), its file's group first, then best score, catalogue order between
   equals; task snippets only in a task file.  Returns how many. */
static int rank(Editor *e, const char *q, size_t nq)
{
	int n = 0;
	const char *g = group(e->buf);
	if (q && match_init(&m, q, nq)) return 0;
	for (int i = 0; i < nsn; i++) {
		if (heading_has(sn[i].head, "task") && !(g && !strcmp(g, "task"))) continue;
		int s = q ? match_score(&m, sn[i].name, strlen(sn[i].name), NULL, 0) : 1, j = n;
		if (s <= 0) continue;
		if (g && heading_has(sn[i].head, g)) s += 1 << 24;
		for (; j && score[j - 1] < s; j--) items[j] = items[j - 1], score[j] = score[j - 1];
		items[j] = (PumItem){ sn[i].name, "Snippet", sn[i].desc };
		score[j] = s;
		n++;
	}
	return n;
}

/* The popup for the word before the cursor (cmp's keyword pattern
   \h\w*\%(-\w*\)*); it closes when there is none or nothing matches. */
static void popup(Editor *e)
{
	size_t len, col = e->win->col, a = col;
	const unsigned char *l = core_line(e, e->win->line, &len);
	while (a && word(l[a - 1])) a--;
	while (a < col && !alpha((char)l[a])) a++;
	core_pum(e, items, a < col ? rank(e, (const char *)l + a, col - a) : 0, -1, a);
}

static const char *body_of(const char *name)
{
	for (int i = 0; i < nsn; i++) if (!strcmp(sn[i].name, name)) return sn[i].body;
	return NULL;
}

const char *snip_get(Editor *e, int i, const char **desc, const char **body)
{
	static int n;
	if (!loaded) load();
	if (!i) n = rank(e, NULL, 0);
	if (i >= n) return NULL;
	*desc = items[i].menu;
	*body = body_of(items[i].word);
	return items[i].word;
}

int snip_insert(Editor *e, const char *name)
{
	const char *body = body_of(name);
	size_t a = cursor(e), z = a, len;
	if (!body) { core_msg(e, "no snippet %s", name); return -1; }
	if (e->ks.visual) {
		/* the Visual area: characters from one end to the other, whole,
		   or the lines without the last one's break */
		size_t l1 = e->ks.vline, c1 = e->ks.vcol, l2 = e->win->line, c2 = e->win->col;
		if (l1 > l2 || (l1 == l2 && c1 > c2)) l1 = l2, c1 = c2, l2 = e->ks.vline, c2 = e->ks.vcol;
		const unsigned char *l = core_line(e, l2, &len);
		a = core_line_start(e, l1) + c1;
		z = core_line_start(e, l2) + (c2 < len ? c2 + uni_glen(l, len, c2) : len + 1);
		if (e->ks.visual == 'V') a = core_line_start(e, l1) + core_first_nonblank(e, l1), z = core_line_start(e, l2) + len;
		if (z > text_len(e->buf->text)) z = text_len(e->buf->text);
		keys_feed(e, KEY_ESC, 0);
	}
	char *sel = malloc(z - a + 1);
	if (!sel) return -1;
	text_read(e->buf->text, a, sel, z - a);
	set_cursor(e, a);
	int r = expand(e, body, a, z, a < z ? sel : NULL, z - a);
	free(sel);
	return r;
}

/* The popup's keys (cmp's preset.insert and Davy's Tab, S-Tab and Enter):
   1 taken, 0 not a popup key. */
static int pum_key(Editor *e, int k)
{
	int n = e->npum, s = e->pumsel;
	switch (k) {
	case 14: case KEY_DOWN: case KEY_TAB: core_pum(e, items, n, s + 1 < n ? s + 1 : -1, e->pumcol); return 1;
	case 16: case KEY_UP: case KEY_S_TAB: core_pum(e, items, n, s < 0 ? n - 1 : s - 1, e->pumcol); return 1;
	case 5: core_pum(e, NULL, 0, -1, 0); return 1;             /* CTRL-E */
	case 25: if (s < 0) s = 0; break;                          /* CTRL-Y */
	case KEY_ENTER: if (s >= 0) break; core_pum(e, NULL, 0, -1, 0); return 0;   /* only a picked item */
	default: return 0;
	}
	size_t a = core_line_start(e, e->win->line) + e->pumcol;
	core_pum(e, NULL, 0, -1, 0);
	expand(e, body_of(items[s].word), a, cursor(e), NULL, 0);
	return 1;
}

int snip_key(Editor *e, int k, int pending)
{
	if (S.b && (S.b != e->buf || S.nr != e->buf->nr)) stop();   /* BufLeave */
	if (e->pum && e->pum == items && pum_key(e, k)) return 1;
	if (S.b && (k == KEY_TAB || k == KEY_S_TAB) && jump(e, k == KEY_TAB ? 1 : -1, S.s[S.cur].idx)) return 1;
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
	if (e->mode == MODE_INSERT && !pending && (word(k) || k == KEY_BS)) popup(e);
	else if (e->pum == items) core_pum(e, NULL, 0, -1, 0);
	return 1;
}
