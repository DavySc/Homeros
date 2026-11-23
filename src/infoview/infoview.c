/* The infoview's editor (bridge.h): kittymax's kitty-lean-webview.el, its
   half that gives the calls their meaning, in C.  homeros-infoview
   (serve/serve.c) carries the page's EditorApi calls here as lines on its
   stdout and takes InfoviewApi events back on its stdin; this side relays
   them to Lean through lsp.h, makes the page's edits through the core, and
   tells the page where the cursor is.  Lean's answers and notifications go
   to the page as the exact JSON Lean sent, never parsed and printed again
   (slow for a 214 kB goal, and it would lose {} from null): the scanner
   below only finds the few values the calls need.

   Kept per page: its subscriptions, its RPC sessions (kept alive every 10 s
   as vscode-lean4 does: Lean ends a session nobody keeps alive), and
   whether it has had the hello for the server that is up.  The page's
   stream, outbox and timeout are the child's; it says "- P" when it forgets
   a page, and as its lines come in order, no call of a forgotten page ever
   follows (design/specs/Infoview.tla). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "file.h"
#include "loop.h"
#include "lsp.h"
#include "bridge.h"
#include "scan.h"

enum { KEEPALIVE_MS = 10000 };


typedef struct Page {
	char id[65];
	struct { char *name; int n; } *sub;     /* subscribed notifications, counted */
	struct { char *uri, *sid; } *ses;       /* RPC sessions: the file, the id as Lean wrote it */
	int nsub, nses;
	long greeted;                           /* the server (gen) it had the hello for */
	struct Page *next;
} Page;

/* A file's diagnostics as the page wants them: whole, one message
   replacing the last (the infoview keeps one list per file), though Lean
   sends them in pieces (incremental diagnostics, vscode-lean4 joins them
   the same way).  Lean's latest file progress per file too, for a new
   page's hello. */
typedef struct Kept { char *uri, *ver; Str d; char *progress; struct Kept *next; } Kept;

typedef struct { char page[65]; char *id; int connect; char *uri; } Req;

static Editor *ed;
static int proc = -1, open_wanted, ticking;
static intptr_t children;                 /* children started: the current one's number */
static char *redirect;                  /* the child's first line: the file the browser opens */
static Str in, o;                       /* the child's output not yet a line; the line being sent */
static Page *pages;
static Kept *kept;
static char *init, *loc;                /* Lean's InitializeResult; the cursor's location, as JSON */
static long gen;                        /* initialize answers seen: each is a server (re)start */

/* The infoview's settings (VS Code's lean4.infoview.*), kittymax's. */
static const char config[] =
	"{\"allErrorsOnLine\":true,\"autoOpenShowsGoal\":true,\"debounceTime\":50,"
	"\"expectedTypeVisibility\":\"Expanded by default\",\"showGoalNames\":true,"
	"\"emphasizeFirstGoal\":false,\"reverseTacticState\":false,\"hideTypeAssumptions\":false,"
	"\"hideInstanceAssumptions\":false,\"hideInaccessibleAssumptions\":false,\"hideLetValues\":false,"
	"\"showTooltipOnHover\":true,\"messageOrder\":\"Sort by proximity to text cursor\"}";

/* --- to the child --------------------------------------------------------- */

/* o, ended, as one line: its line breaks (inside Lean's JSON only ever
   whitespace) become spaces. */
static void send_line(void)
{
	for (size_t i = 0; i < o.n; i++) if (o.s[i] == '\n' || o.s[i] == '\r') o.s[i] = ' ';
	str_add(&o, "\n", 1);
	if (proc >= 0) proc_write(proc, o.s, o.n);
	o.n = 0;
}

static void to(const char *page) { o.n = 0; str_addz(&o, page); str_add(&o, " ", 1); }

/* An InfoviewApi call with one argument (raw JSON). */
static void event(const char *page, const char *m, const char *arg, size_t n)
{
	to(page);
	str_addz(&o, "{\"m\":\""); str_addz(&o, m); str_addz(&o, "\",\"a\":[");
	str_add(&o, arg, n);
	str_addz(&o, "]}");
	send_line();
}

/* One of Lean's messages to the page as gotServerNotification (or, out,
   sentClientNotification): host.js calls api[m](n, raw.params). */
static void notification(const char *page, int out, const char *name, size_t nn, const char *msg, size_t n)
{
	to(page);
	str_addz(&o, out ? "{\"m\":\"sentClientNotification\",\"n\":\"" : "{\"m\":\"gotServerNotification\",\"n\":\"");
	str_add(&o, name, nn);
	str_addz(&o, "\",\"raw\":");
	str_add(&o, msg, n);
	str_addz(&o, "}");
	send_line();
}

/* The answer to page's call id: kind "raw" (Lean's whole response), "val"
   or "err" (v a JSON value). */
static void answer(const char *page, const char *id, const char *kind, const char *v, size_t n)
{
	to(page);
	str_addz(&o, "{\"id\":"); str_addz(&o, id); str_addz(&o, ",\"k\":\""); str_addz(&o, kind); str_addz(&o, "\",\"v\":");
	str_add(&o, v, n);
	str_addz(&o, "}");
	send_line();
}

static void fail(const char *page, const char *id, const char *why, const char *what)
{
	Str m = { 0 };
	str_addz(&m, "{\"message\":\"");
	j_quote(&m, why, strlen(why));
	if (what) j_quote(&m, what, strlen(what));
	str_addz(&m, "\"}");
	if (m.s) answer(page, id, "err", m.s, m.n);
	free(m.s);
}

/* --- pages ---------------------------------------------------------------- */

static Page *find(const char *id)
{
	for (Page *p = pages; p; p = p->next) if (!strcmp(p->id, id)) return p;
	return NULL;
}

static Page *page(const char *id)
{
	Page *p = find(id);
	if (p || !(p = calloc(1, sizeof *p))) return p;
	snprintf(p->id, sizeof p->id, "%s", id);
	p->next = pages;
	return pages = p;
}

static void drop(Page *p)
{
	Page **q = &pages;
	while (*q != p) q = &(*q)->next;
	*q = p->next;
	for (int i = 0; i < p->nsub; i++) free(p->sub[i].name);
	for (int i = 0; i < p->nses; i++) { free(p->ses[i].uri); free(p->ses[i].sid); }
	free(p->sub); free(p->ses); free(p);
}

static int subscribed(const Page *p, const char *name, size_t n)
{
	for (int i = 0; i < p->nsub; i++)
		if (strlen(p->sub[i].name) == n && !memcmp(p->sub[i].name, name, n)) return 1;
	return 0;
}

static void diagnostics(const char *page, const Kept *k)
{
	Str m = { 0 };
	str_addz(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"");
	str_addz(&m, k->uri);
	str_addz(&m, "\",\"version\":"); str_addz(&m, k->ver);
	str_addz(&m, ",\"diagnostics\":["); str_add(&m, k->d.s, k->d.n); str_addz(&m, "]}}");
	if (m.s) notification(page, 0, "textDocument/publishDiagnostics", 31, m.s, m.n);
	free(m.s);
}

/* What a page needs before it shows anything: VS Code's initInfoView.
   Once per page and server, when both a server and a Lean file are known
   (test/kitty_lean_greet.tla in kittymax: a buffer joining the same server
   is no restart, which would drop the page's RPC sessions). */
static void greet(Page *p)
{
	if (!init || !loc || p->greeted == gen) return;
	p->greeted = gen;
	event(p->id, "changedCursorLocation", loc, strlen(loc));
	event(p->id, "initialize", loc, strlen(loc));
	event(p->id, "changedInfoviewConfig", config, sizeof config - 1);
	event(p->id, "serverStopped", "null", 4);
	event(p->id, "serverRestarted", init, strlen(init));
	for (Kept *k = kept; k; k = k->next) {
		if (k->ver) diagnostics(p->id, k);
		if (k->progress) notification(p->id, 0, "$/lean/fileProgress", 19, k->progress, strlen(k->progress));
	}
}

static Kept *kept_for(const char *uri, size_t n)
{
	for (Kept *k = kept; k; k = k->next) if (strlen(k->uri) == n && !memcmp(k->uri, uri, n)) return k;
	Kept *k = calloc(1, sizeof *k);
	if (!k || !(k->uri = j_copy(uri, n))) { free(k); return NULL; }
	k->next = kept;
	return kept = k;
}

/* --- Lean to the pages (lsp_tap) ----------------------------------------- */

static void tap(int out, const char *m, size_t n)
{
	size_t meth = j_member(m, n, 0, "method"), p = j_member(m, n, 0, "params");
	if (meth == NONE) {
		/* the answer to initialize: a server (re)started */
		size_t r = j_member(m, n, 0, "result");
		if (r == NONE) return;
		free(init);
		init = j_copy(m + r, j_len(m, n, r));
		gen++;
		for (Page *q = pages; q; q = q->next) greet(q);
		return;
	}
	const char *name = m + meth + 1;
	size_t nn = j_len(m, n, meth) - 2;
	if (!out && j_is(m, n, meth, "textDocument/publishDiagnostics")) {
		size_t u = j_member(m, n, p, "uri"), d = j_member(m, n, p, "diagnostics"), v = j_member(m, n, p, "version");
		Kept *k = u == NONE || d == NONE ? NULL : kept_for(m + u + 1, j_len(m, n, u) - 2);
		if (!k) return;
		size_t inc = j_member(m, n, p, "isIncremental");
		if (inc == NONE || n - inc < 4 || memcmp(m + inc, "true", 4)) k->d.n = 0;
		free(k->ver);
		k->ver = v == NONE ? j_copy("null", 4) : j_copy(m + v, j_len(m, n, v));
		size_t dz = d + j_len(m, n, d) - 1, di = j_ws(m, n, d + 1);
		if (di < dz) {
			if (k->d.n) str_add(&k->d, ",", 1);
			str_add(&k->d, m + di, dz - di);
		}
		for (Page *q = pages; q; q = q->next)
			if (subscribed(q, name, nn)) diagnostics(q->id, k);
		return;
	}
	if (!out && j_is(m, n, meth, "$/lean/fileProgress")) {
		size_t u = j_member(m, n, j_member(m, n, p, "textDocument"), "uri");
		Kept *k = u == NONE ? NULL : kept_for(m + u + 1, j_len(m, n, u) - 2);
		if (k) { free(k->progress); k->progress = j_copy(m, n); }
	}
	for (Page *q = pages; q; q = q->next)
		if (subscribed(q, name, nn)) notification(q->id, out, name, nn, m, n);
}

/* What lsp holds already, for a bridge started after Lean spoke: Lean's
   diagnostics come once per change, so they are made again from lsp_diags
   (where they are now; without what lsp drops, as isSilent). */
static void seed(void)
{
	for (Buf *b = ed->bufs; b; b = b->next) {
		const char *u = b->lsp ? lsp_uri(b->lsp) : NULL, *m;
		size_t mn;
		int nd;
		/* a server that answered initialize before the bridge listened */
		if (u && !init && (m = lsp_init(b->lsp, &mn))) tap(0, m, mn);
		const LspDiag *d = u ? lsp_diags(b->lsp, &nd) : NULL;
		Kept *k = d ? kept_for(u, strlen(u)) : NULL;
		if (!k || k->ver) continue;
		k->ver = j_copy("null", 4);
		for (int i = 0; i < nd; i++) {
			size_t at[2] = { d[i].a, d[i].z }, l[2], c[2];
			for (int j = 0; j < 2; j++) {
				char line[4096];
				l[j] = text_line_of(b->text, at[j]);
				size_t a = text_line_start(b->text, l[j]), w = at[j] - a;
				char *s = w <= sizeof line ? line : malloc(w);
				c[j] = s ? lsp_u16((const unsigned char *)s, text_read(b->text, a, s, w)) : 0;
				if (s != line) free(s);
			}
			char r[160];
			snprintf(r, sizeof r, "%s{\"range\":{\"start\":{\"line\":%zu,\"character\":%zu},"
			         "\"end\":{\"line\":%zu,\"character\":%zu}},\"severity\":%d,\"message\":\"",
			         i ? "," : "", l[0], c[0], l[1], c[1], d[i].sev);
			str_addz(&k->d, r);
			j_quote(&k->d, d[i].msg, strlen(d[i].msg));
			str_addz(&k->d, "\"}");
		}
	}
}

/* --- the page to Lean and the editor (EditorApi) ------------------------- */

static void keepalive(void *ctx)
{
	(void)ctx;
	ticking = 0;
	for (Page *p = pages; p; p = p->next)
		for (int i = 0; i < p->nses; i++) {
			Str m = { 0 };
			str_addz(&m, "{\"uri\":\""); str_addz(&m, p->ses[i].uri);
			str_addz(&m, "\",\"sessionId\":"); str_addz(&m, p->ses[i].sid); str_addz(&m, "}");
			if (m.s) lsp_send(p->ses[i].uri, "$/lean/rpc/keepAlive", m.s, m.n, NULL, NULL);
			free(m.s);
			ticking = 1;
		}
	if (ticking) loop_timer(keepalive, NULL, KEEPALIVE_MS);
}

static void answered(void *ctx, const char *m, size_t n)
{
	Req *r = ctx;
	Page *p = find(r->page);    /* NULL: forgotten meanwhile, its answers dropped */
	if (!p) ;
	else if (!m) fail(r->page, r->id, "The Lean server stopped", NULL);
	else if (!r->connect) answer(r->page, r->id, "raw", m, n);
	else {
		size_t res = j_member(m, n, 0, "result"), s = j_member(m, n, res, "sessionId"), e = j_member(m, n, 0, "error");
		void *g = s == NONE ? NULL : realloc(p->ses, (size_t)(p->nses + 1) * sizeof *p->ses);
		if (s == NONE) {
			if (e != NONE) answer(r->page, r->id, "err", m + e, j_len(m, n, e));
			else fail(r->page, r->id, "No session", NULL);
		} else if (g) {
			p->ses = g;
			p->ses[p->nses].uri = r->uri;
			p->ses[p->nses++].sid = j_copy(m + s, j_len(m, n, s));
			r->uri = NULL;
			answer(r->page, r->id, "val", m + s, j_len(m, n, s));
			if (!ticking) { ticking = 1; loop_timer(keepalive, NULL, KEEPALIVE_MS); }
		}
	}
	free(r->uri); free(r->id); free(r);
}

static Buf *buf_of(const char *uri)
{
	for (Buf *b = ed->bufs; b; b = b->next)
		if (b->lsp && lsp_uri(b->lsp) && !strcmp(lsp_uri(b->lsp), uri)) return b;
	return NULL;
}

/* Makes uri's buffer current, opening the file if it is not open yet. */
static int visit(const char *uri)
{
	Buf *b = buf_of(uri);
	if (b == ed->buf) return 0;
	if (b) return core_buf_goto(ed, b->nr);
	if (strncmp(uri, "file://", 7)) return -1;
	char *path = malloc(strlen(uri)), *d = path;
	if (!path) return -1;
	for (const char *s = uri + 7; *s; s++) {
		long c = *s == '%' && s[1] && s[2] ? j_hex4((const char[]){ '0', '0', s[1], s[2] }, 4, 0) : -1;
		if (c >= 0) { *d++ = (char)c; s += 2; } else *d++ = *s;
	}
	*d = 0;
	int r = core_edit_file(ed, path, 0);
	free(path);
	return r;
}

/* The byte of LSP position pos (an object) in the current buffer. */
static size_t byte(const char *s, size_t n, size_t pos, size_t *line)
{
	size_t lv = j_member(s, n, pos, "line"), cv = j_member(s, n, pos, "character"), nl = core_lines(ed), w;
	/* s ends in the line's '\0' (from_child), so strtol stops there */
	long l = lv == NONE ? 0 : strtol(s + lv, NULL, 10), c = cv == NONE ? 0 : strtol(s + cv, NULL, 10);
	if (l < 0 || c < 0) l = c = 0;
	if ((size_t)l >= nl) { l = (long)nl - 1; c = 1L << 30; }
	const unsigned char *t = core_line(ed, (size_t)l, &w);
	if (line) *line = (size_t)l;
	return core_line_start(ed, (size_t)l) + lsp_u16_byte(t, w, (size_t)c);
}

static void moved(size_t at)
{
	size_t l = text_line_of(ed->buf->text, at);
	core_goto(ed, l, at - core_line_start(ed, l));
}

/* An edit from the page is a group of its own (DESIGN.md: a server edit),
   sent to Lean at once: it comes between commands. */
static void edited(void)
{
	undo_close(ed->buf->undo);
	lsp_flush(ed->buf->lsp);
	core_changed = 1;
}

typedef struct { size_t a, z; char *t; size_t n; } Ed;

static int later(const void *x, const void *y)
{
	const Ed *a = x, *b = y;
	return (a->a < b->a) - (a->a > b->a);
}

/* The TextEdits in the array at arr, to uri's buffer: from the last to the
   first, so each range is still where the server meant it. */
static int edits(const char *s, size_t n, const char *uri, size_t arr)
{
	int ne = 0, r = 0;
	Ed *v = NULL;
	if (visit(uri)) return -1;
	undo_close(ed->buf->undo);
	for (size_t e; (e = j_elem(s, n, arr, ne)) != NONE; ne++) {
		Ed *g = realloc(v, (size_t)(ne + 1) * sizeof *v);
		if (!g) { r = -1; break; }
		v = g;
		size_t rg = j_member(s, n, e, "range");
		v[ne].a = byte(s, n, j_member(s, n, rg, "start"), NULL);
		v[ne].z = byte(s, n, j_member(s, n, rg, "end"), NULL);
		if (v[ne].z < v[ne].a) v[ne].z = v[ne].a;
		v[ne].t = j_str(s, n, j_member(s, n, e, "newText"), &v[ne].n);
		if (!v[ne].t) v[ne].t = j_copy("", 0), v[ne].n = 0;
	}
	if (ne) qsort(v, (size_t)ne, sizeof *v, later);
	for (int i = 0; i < ne; i++) {
		if (!r && v[i].t) r = core_edit(ed, v[i].a, v[i].z - v[i].a, v[i].t, v[i].n);
		free(v[i].t);
	}
	free(v);
	edited();
	return r;
}

/* WorkspaceEdit: changes {uri: [TextEdit]} or documentChanges
   [{textDocument, edits}]. */
static int apply(const char *s, size_t n, size_t we)
{
	size_t ch = j_member(s, n, we, "changes"), dc = j_member(s, n, we, "documentChanges"), e;
	int r = 0;
	for (int k = 0; (e = j_elem(s, n, dc, k)) != NONE; k++) {
		char *u = j_str(s, n, j_member(s, n, j_member(s, n, e, "textDocument"), "uri"), NULL);
		r |= u ? edits(s, n, u, j_member(s, n, e, "edits")) : -1;
		free(u);
	}
	if (ch != NONE && s[ch] == '{')
		for (size_t i = j_ws(s, n, ch + 1); i < n && s[i] == '"'; ) {
			char *u = j_str(s, n, i, NULL);
			size_t v = j_ws(s, n, j_skip(s, n, i) + 1);
			r |= u ? edits(s, n, u, v) : -1;
			free(u);
			if ((i = j_next(s, n, v)) == NONE) break;
		}
	return r;
}

static void call(const char *pid, char *s, size_t n)
{
	size_t iv = j_member(s, n, 0, "id"), m = j_member(s, n, 0, "m"), a = j_member(s, n, 0, "a");
	if (iv == NONE || m == NONE) return;    /* no JSON, or no id to answer */
	char *id = j_copy(s + iv, j_len(s, n, iv));
	Page *p = page(pid);
	size_t a0 = j_elem(s, n, a, 0), a1 = j_elem(s, n, a, 1), a2 = j_elem(s, n, a, 2);
	char *uri = a0 != NONE && s[a0] == '"' ? j_str(s, n, a0, NULL) : NULL;
	if (!id || !p) goto out;
	if (j_is(s, n, m, "sendClientRequest") || j_is(s, n, m, "createRpcSession")) {
		int c = j_is(s, n, m, "createRpcSession");
		Req *r = calloc(1, sizeof *r);
		char *meth = c ? j_copy("$/lean/rpc/connect", 18) : j_str(s, n, a1, NULL);
		Str prm = { 0 };
		if (c) { str_addz(&prm, "{\"uri\":"); if (a0 != NONE) str_add(&prm, s + a0, j_len(s, n, a0)); str_addz(&prm, "}"); }
		else if (a2 != NONE) str_add(&prm, s + a2, j_len(s, n, a2));
		if (r && meth && uri && prm.s) {
			snprintf(r->page, sizeof r->page, "%s", pid);
			r->id = id; id = NULL;
			r->connect = c;
			r->uri = uri; uri = NULL;
			if (lsp_send(r->uri, meth, prm.s, prm.n, answered, r)) {
				fail(pid, r->id, "No Lean server for ", r->uri);
				free(r->uri); free(r->id); free(r);
			}
		} else {
			fail(pid, id, "No Lean server for ", uri ? uri : "");
			free(r);
		}
		free(meth); free(prm.s);
	} else if (j_is(s, n, m, "sendClientNotification")) {
		char *meth = j_str(s, n, a1, NULL);
		if (uri && meth && a2 != NONE) lsp_send(uri, meth, s + a2, j_len(s, n, a2), NULL, NULL);
		free(meth);
		answer(pid, id, "val", "null", 4);
	} else if (j_is(s, n, m, "subscribeServerNotifications") || j_is(s, n, m, "subscribeClientNotifications") ||
	           j_is(s, n, m, "unsubscribeServerNotifications") || j_is(s, n, m, "unsubscribeClientNotifications")) {
		int i = 0, un = s[m + 1] == 'u';
		while (uri && i < p->nsub && strcmp(p->sub[i].name, uri)) i++;
		if (!uri) ;
		else if (un && i < p->nsub && !--p->sub[i].n) { free(p->sub[i].name); p->sub[i] = p->sub[--p->nsub]; }
		else if (!un && i < p->nsub) p->sub[i].n++;
		else if (!un) {
			void *g = realloc(p->sub, (size_t)(p->nsub + 1) * sizeof *p->sub);
			if (g) { p->sub = g; p->sub[p->nsub].name = uri; p->sub[p->nsub++].n = 1; uri = NULL; }
		}
		answer(pid, id, "val", "null", 4);
	} else if (j_is(s, n, m, "closeRpcSession")) {
		for (int i = 0; i < p->nses; i++)
			if (j_len(s, n, a0) == strlen(p->ses[i].sid) && !memcmp(s + a0, p->ses[i].sid, j_len(s, n, a0))) {
				free(p->ses[i].uri); free(p->ses[i].sid);
				p->ses[i--] = p->ses[--p->nses];
			}
		answer(pid, id, "val", "null", 4);
	} else if (j_is(s, n, m, "saveConfig")) {
		answer(pid, id, "val", "null", 4);
	} else if (j_is(s, n, m, "insertText")) {
		/* text, "above" or "here", and where (else the cursor) */
		size_t tn, at, l;
		char *t = j_str(s, n, a0, &tn), *u = j_str(s, n, j_member(s, n, j_member(s, n, a2, "textDocument"), "uri"), NULL);
		int r = !t || (u && visit(u));
		if (!r) {
			undo_close(ed->buf->undo);
			if (u) moved(byte(s, n, j_member(s, n, a2, "position"), NULL));
			l = ed->win->line;
			at = core_line_start(ed, l) + ed->win->col;
			if (j_is(s, n, a1, "above")) {
				size_t w = core_first_nonblank(ed, l), ln;
				const unsigned char *b = core_line(ed, l, &ln);
				Str x = { 0 };
				str_add(&x, (const char *)b, w); str_add(&x, t, tn); str_add(&x, "\n", 1);
				at = core_line_start(ed, l);
				r = !x.s || core_edit(ed, at, 0, x.s, x.n);
				free(x.s);
			} else r = core_edit(ed, at, 0, t, tn) != 0;
			edited();
		}
		if (r) fail(pid, id, "Could not insert the text", NULL);
		else answer(pid, id, "val", "null", 4);
		free(t); free(u);
	} else if (j_is(s, n, m, "applyEdit")) {
		if (apply(s, n, a0)) fail(pid, id, "Could not apply the edit", NULL);
		else answer(pid, id, "val", "null", 4);
	} else if (j_is(s, n, m, "showDocument")) {
		char *u = j_str(s, n, j_member(s, n, a0, "uri"), NULL);
		size_t sel = j_member(s, n, a0, "selection");
		if (!u || visit(u)) fail(pid, id, "Could not open ", u);
		else {
			if (sel != NONE) { core_setpcmark(ed); moved(byte(s, n, j_member(s, n, sel, "start"), NULL)); }
			core_changed = 1;
			answer(pid, id, "val", "true", 4);
		}
		free(u);
	} else if (j_is(s, n, m, "restartFile")) {
		Buf *b = uri ? buf_of(uri) : NULL;
		if (b) lsp_restart(b->lsp);
		answer(pid, id, "val", "null", 4);
	} else fail(pid, id, "Unknown EditorApi call", NULL);
out:
	free(id);
	free(uri);
}

/* --- the child ------------------------------------------------------------ */

/* The browser, sent to the page by the child's private file, so the token
   is never on a command line */
static void browse(void)
{
	if (proc_open(redirect)) core_msg(ed, "Infoview: cannot open %s", redirect);
}

static void from_child(void *ctx, char *l, size_t n)
{
	(void)ctx;
	if (!redirect) {
		redirect = j_copy(l, n);
		if (open_wanted) browse();
		return;
	}
	char *sp = memchr(l, ' ', n);
	if (!sp) return;
	*sp = 0;
	if (!strcmp(l, "+")) { Page *p = page(sp + 1); if (p) greet(p); }
	else if (!strcmp(l, "-")) { Page *p = find(sp + 1); if (p) drop(p); }
	else call(l, sp + 1, n - (size_t)(sp + 1 - l));
}

static void out(void *ctx, int stream, const char *b, size_t n)
{
	/* a child let go by bridge_free may still end after its successor
	   started: what it says is no longer ours */
	if ((intptr_t)ctx != children) return;
	if (stream == 0) {     /* the child is gone: so are its pages */
		proc = -1;
		while (pages) drop(pages);
		free(redirect);
		redirect = NULL;
		in.n = 0;
		return;
	}
	if (stream == 1) str_lines(&in, b, n, from_child, NULL);
}

/* The child's ROOT: $HOMEROS_INFOVIEW, else stdpath("data")/infoview,
   where src/infoview/install.sh puts it. */
static int root(char *r, size_t n)
{
	const char *v = getenv("HOMEROS_INFOVIEW");
	if (v && *v) snprintf(r, n, "%s", v);
	else file_stdpath(r, n, 0, "/infoview");
	return strlen(r) < n - 40 ? 0 : -1;
}

int bridge_open(Editor *e)
{
	char r[4000], dist[4096], exe[4096];
	ed = e;
	if (proc < 0) {
		if (root(r, sizeof r)) { core_msg(e, "Infoview: no place for it"); return -1; }
		snprintf(dist, sizeof dist, "%s/dist/index.production.min.js", r);
		if (access(dist, R_OK)) { core_msg(e, "Infoview: none in %s (src/infoview/install.sh)", r); return -1; }
		/* the child beside the editor's binary, else on PATH */
#ifdef _WIN32
		strcpy(exe, "homeros-infoview");   /* CreateProcess looks beside the editor first */
#else
		ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 20);
		exe[k > 0 ? k : 0] = 0;
		char *slash = strrchr(exe, '/');
		if (slash) strcpy(slash, "/homeros-infoview");
		if (!slash || access(exe, X_OK)) strcpy(exe, "homeros-infoview");
#endif
		char *argv[] = { exe, r, NULL };
		if ((proc = proc_spawn_ov(argv, NULL, out, (void *)++children)) < 0) { core_msg(e, "Infoview: %s does not start", exe); return -1; }
		lsp_tap = tap;
		seed();
	}
	open_wanted = 1;
	if (redirect) browse();
	core_msg(e, "Infoview: opening it in the browser");
	return 0;
}

void bridge_update(Editor *e)
{
	ed = e;
	const char *u = proc >= 0 && e->buf->lsp ? lsp_uri(e->buf->lsp) : NULL;
	size_t ul = u ? strlen(u) : 0;
	if (ul < 5 || strcmp(u + ul - 5, ".lean")) return;
	size_t pt[2][2] = { { e->win->line, e->win->col }, { e->win->line, e->win->col } }, w;
	if (e->ks.visual) {
		/* a selection is a range, its ends in order */
		int before = e->ks.vline < e->win->line || (e->ks.vline == e->win->line && e->ks.vcol < e->win->col);
		pt[!before][0] = e->ks.vline;
		pt[!before][1] = e->ks.vcol;
	}
	char j[512];
	size_t c[2];
	for (int i = 0; i < 2; i++) {
		const unsigned char *t = core_line(e, pt[i][0], &w);
		c[i] = lsp_u16(t, pt[i][1] < w ? pt[i][1] : w);
	}
	snprintf(j, sizeof j, "{\"start\":{\"line\":%zu,\"character\":%zu},\"end\":{\"line\":%zu,\"character\":%zu}}",
	         pt[0][0], c[0], pt[1][0], c[1]);
	Str l = { 0 };
	str_addz(&l, "{\"uri\":\""); str_addz(&l, u); str_addz(&l, "\",\"range\":"); str_addz(&l, j); str_addz(&l, "}");
	if (!l.s || (loc && !strcmp(loc, l.s))) { free(l.s); return; }
	free(loc);
	loc = l.s;
	event("*", "changedCursorLocation", loc, l.n);
	for (Page *p = pages; p; p = p->next) greet(p);
}

void bridge_free(void)
{
	if (proc >= 0) proc_close(proc);   /* at EOF it deletes its file and ends */
	proc = -1;
	lsp_tap = NULL;
	while (pages) drop(pages);
	while (kept) { Kept *k = kept; kept = k->next; free(k->uri); free(k->ver); free(k->d.s); free(k->progress); free(k); }
	free(redirect); free(init); free(loc); free(in.s); free(o.s);
	redirect = init = loc = NULL;
	in = o = (Str){ 0 };
	gen = open_wanted = 0;
}
