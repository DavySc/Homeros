/* The LSP client (lsp.h).  Servers are child processes of the loop
   (loop.h proc_spawn); messages are JSON-RPC with Content-Length frames
   on their stdin and stdout.  The rules for what is shown are
   LspSync.tla's "ok" variant, which is VS Code's: a document's version
   counts the didChanges sent; every edit is logged in LSP's coordinates,
   and what the server says about an older version is mapped through the
   edits made since, the log pruned up to it.  The "simple" variant
   (ignore older versions) hid Lean's diagnostics for as long as typing
   went on, at a key every 250 ms (src/lsp/bench/stale.c), against the
   design's 2 s limit.  Replies are not mapped (LspSync "mapreply" fails):
   one is shown only while the text is as it was when asked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "core.h"
#include "json.h"
#include "loop.h"
#include "lsp.h"

static const char *const lean_argv[] = { "lake", "serve", NULL };
static const char *const lean_roots[] = { "lakefile.lean", "lakefile.toml", "lean-toolchain", NULL };
static const char *const clangd_argv[] = { "clangd", NULL };
static const char *const clangd_roots[] = { "compile_commands.json", "compile_flags.txt", ".git", NULL };
static const char *const ra_argv[] = { "rust-analyzer", NULL };
static const char *const ra_roots[] = { "Cargo.toml", NULL };
static const LspServer table[] = {
	{ ".lean", "lean4", lean_argv, lean_roots, 1 },
	{ ".c", "c", clangd_argv, clangd_roots, 0 },
	{ ".h", "c", clangd_argv, clangd_roots, 0 },
	{ ".cpp", "cpp", clangd_argv, clangd_roots, 0 },
	{ ".hpp", "cpp", clangd_argv, clangd_roots, 0 },
	{ ".rs", "rust", ra_argv, ra_roots, 0 },
	{ NULL, NULL, NULL, NULL, 0 },
};
const LspServer *lsp_servers = table;

typedef struct { LspDiag *v; int n, cap; long ver; } Set;   /* ver: the version it came for */

/* An edit in LSP's coordinates: [s, e) became text ending at i, made
   while the document was version v. */
typedef struct { size_t l, c; } Pos;
typedef struct { long v; Pos s, e, i; } Ed;
enum { LOG = 4096 };

typedef struct Srv {
	const LspServer *def;
	char *root;
	int proc;               /* -1: not running */
	int ready;              /* initialize answered: messages may go */
	int closing;            /* its last file closed: freed once it exits */
	Str in, held;           /* bytes read and not yet handled; messages waiting for ready */
	Str init;               /* a Lean server's answer to initialize, for a bridge opened later */
	int work;               /* $/progress tokens begun and not ended; -1 before any */
	long next;              /* request ids; 0 is initialize */
	LspDoc *docs;
	struct Srv *link;
} Srv;

struct LspDoc {
	Srv *s;
	char *uri;
	const Text *t;
	long ver;               /* didChanges sent */
	int bad;                /* holds what is not UTF-8: changes go as the whole text */
	Str chg;                /* this command's contentChanges, not yet sent */
	Set diag, prog;
	Ed *log;                /* the edits since floor, oldest first */
	int nlog;
	long floor;             /* the oldest version the log can map from */
	struct { long id, ver; char *ans; } ask[LSP_NASK];   /* id 0: no reply pending */
	LspDoc *link;
};

static Srv *srvs;
void (*lsp_tap)(int out, const char *msg, size_t n);
/* lsp_send's requests waiting for their answers */
typedef struct Raw { Srv *s; long id; LspRawFn fn; void *ctx; struct Raw *link; } Raw;
static Raw *raws;
static void out(void *ctx, int stream, const char *b, size_t n);
static Str o, line;             /* the message being built; one line of text */
static Jtok *tk;
static int ntk;

/* file:// and the absolute path, percent-encoded but for / and RFC 3986's
   unreserved characters (VS Code's). */
static char *uri(const char *abs)
{
	Str u = { 0 };
	if (abs[0] == '/') str_lit(&u, "file://");
	else str_lit(&u, "file:///");        /* C:/x is file:///C:/x (RFC 8089) */
	for (const unsigned char *p = (const unsigned char *)abs; *p; p++)
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("/-._~:", *p))
			str_add(&u, (const char *)p, 1);
		else str_addf(&u, "%%%02X", *p);
	return u.s;
}

/* --- sending ------------------------------------------------------------ */

/* o, framed, to the server; held until it has answered initialize (the
   spec allows nothing else before), unless now. */
static void frame(Srv *s, int now)
{
	char h[40];
	int n = snprintf(h, sizeof h, "Content-Length: %zu\r\n\r\n", o.n);
	if (s->proc < 0) return;
	/* the infoview hears the edits as Lean does (vscode-lean4's
	   sentClientNotification) */
	if (lsp_tap && s->def->lean && (!strncmp(o.s, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\"", 50) ||
	                                !strncmp(o.s, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\"", 49)))
		lsp_tap(1, o.s, o.n);
	if (!s->ready && !now) { str_add(&s->held, h, (size_t)n); str_add(&s->held, o.s, o.n); return; }
	proc_write(s->proc, h, (size_t)n);
	proc_write(s->proc, o.s, o.n);
}

static void send(Srv *s) { frame(s, 0); }

static void begin(const char *method)
{
	o.n = 0;
	str_addf(&o, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":", method);
}

static void end(Srv *s) { str_lit(&o, "}"); send(s); }

/* Bytes [a, a + n) of t, in line's buffer; n 0 when out of memory. */
static const unsigned char *bytes(const Text *t, size_t a, size_t *n)
{
	if (line.cap < *n + 1) {
		char *b = realloc(line.s, *n + 1);
		if (!b) { *n = 0; return (const unsigned char *)""; }
		line.s = b; line.cap = *n + 1;
	}
	*n = text_read(t, a, line.s, *n);
	return (const unsigned char *)line.s;
}

/* Line and UTF-16 column of byte pos in t. */
static void where(const Text *t, size_t pos, size_t *l, size_t *c)
{
	*l = text_line_of(t, pos);
	size_t a = text_line_start(t, *l), n = pos - a;
	const unsigned char *b = bytes(t, a, &n);
	*c = lsp_u16(b, n);
}

static void putpos(const Text *t, size_t pos)
{
	size_t l, c;
	where(t, pos, &l, &c);
	str_addf(&o, "{\"line\":%zu,\"character\":%zu}", l, c);
}

/* The whole text as a JSON string; whether it holds what is not UTF-8.
   Read in one piece: U+FFFD for a byte depends on the bytes after it. */
static int put_text(const Text *t)
{
	size_t n = text_len(t);
	const unsigned char *b = bytes(t, 0, &n);
	return str_addq(&o, (const char *)b, n) != 0;
}

static void open_doc(LspDoc *d)
{
	begin("textDocument/didOpen");
	str_addf(&o, "{\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"%s\",\"version\":%ld,\"text\":\"",
	         d->uri, d->s->def->lang, d->ver);
	d->bad = put_text(d->t);
	str_lit(&o, "\"}}");
	end(d->s);
}

static void start(Srv *s)
{
	s->ready = 0;
	s->in.n = s->held.n = 0;
	s->next = 1;
	s->work = -1;
	s->proc = proc_spawn((char *const *)s->def->argv, s->root, out, s);
	if (s->proc < 0) return;
	char *u = uri(s->root);
	o.n = 0;
	str_addf(&o, "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{\"processId\":%ld,\"rootUri\":\"", (long)getpid());
	if (u) str_add(&o, u, strlen(u));
	free(u);
	/* Lean: incremental diagnostics (else 420 MB of JSON, measured in
	   kittymax) and widgets (the infoview, package 14); $/progress for the
	   status line (lualine's lsp_status) */
	str_lit(&o, "\",\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{\"versionSupport\":true},"
	        "\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},\"synchronization\":{\"didSave\":true}},"
	        "\"window\":{\"workDoneProgress\":true},\"general\":{\"positionEncodings\":[\"utf-16\"]},\"lean\":{\"incrementalDiagnosticSupport\":true}}");
	if (s->def->lean) str_lit(&o, ",\"initializationOptions\":{\"hasWidgets\":true}");
	str_lit(&o, "}}");
	frame(s, 1);
}

/* --- what is shown ---------------------------------------------------------- */

static void clear(Set *v)
{
	for (int i = 0; i < v->n; i++) free(v->v[i].msg);
	v->n = 0;
}

/* Cancels d's pending request of kind k. */
static void cancel(LspDoc *d, int k)
{
	if (!d->ask[k].id) return;
	begin("$/cancelRequest");
	str_addf(&o, "{\"id\":%ld}", d->ask[k].id);
	end(d->s);
	d->ask[k].id = 0;
}

static void forget(LspDoc *d)
{
	clear(&d->diag);
	clear(&d->prog);
	for (int k = 0; k < LSP_NASK; k++) { free(d->ask[k].ans); d->ask[k].ans = NULL; d->ask[k].id = 0; }
	core_changed = 1;
}

/* Opens d again as a new version, showing nothing, so what the server
   said about the old one, still on its way, is ignored. */
static void reopen(LspDoc *d)
{
	forget(d);
	d->ver++;
	d->floor = d->ver;
	d->nlog = 0;
	open_doc(d);
}

/* Bytes [a, z) of t keep pointing at the same characters while at pos
   ndel bytes become nins (as a delete, then an insert at pos). */
static int shift(LspDiag *g, size_t pos, size_t ndel, size_t nins)
{
	size_t a = g->a < pos ? g->a : (g->a > pos + ndel ? g->a : pos + ndel) - ndel + nins;
	size_t z = g->z <= pos ? g->z : g->z <= pos + ndel ? pos : g->z - ndel + nins;
	if (g->a == g->z) z = a;
	else if (a >= z) return 0;
	g->a = a; g->z = z;
	return 1;
}

static void shift_set(Set *v, size_t pos, size_t ndel, size_t nins)
{
	int k = 0;
	for (int i = 0; i < v->n; i++)
		if (shift(&v->v[i], pos, ndel, nins)) v->v[k++] = v->v[i];
		else free(v->v[i].msg);
	if (k != v->n) core_changed = 1;
	v->n = k;
}

/* --- the public calls ------------------------------------------------------- */

static char *absolute(const char *path)
{
#ifdef _WIN32
	char *abs = _fullpath(NULL, path, 0);   /* realpath's twin; a URI has '/' */
	for (char *p = abs; p && *p; p++) if (*p == '\\') *p = '/';
	return abs;
#else
	char *abs = realpath(path, NULL), cwd[4096];
	if (abs) return abs;
	if (path[0] == '/') return strdup(path);
	Str a = { 0 };
	if (getcwd(cwd, sizeof cwd)) str_addf(&a, "%s/%s", cwd, path);
	return a.s;
#endif
}

/* The nearest directory above file abs holding one of roots, else the
   file's own. */
static char *project(const char *abs, const char *const *roots)
{
	char *dir = strdup(abs), *own = strdup(abs);
	if (!dir || !own) { free(dir); free(own); return NULL; }
	*strrchr(own, '/') = 0;
	for (char *slash; (slash = strrchr(dir, '/')) && slash != dir; ) {
		*slash = 0;
		for (const char *const *m = roots; *m; m++) {
			Str p = { 0 };
			int found = !str_addf(&p, "%s/%s", dir, *m) && !access(p.s, F_OK);
			free(p.s);
			if (found) { free(own); return dir; }
		}
	}
	free(dir);
	return own;
}

LspDoc *lsp_open(const char *path, const Text *t)
{
	const char *dot = strrchr(path, '.');
	const LspServer *def = lsp_servers;
	while (def->ext && (!dot || strcmp(def->ext, dot))) def++;
	if (!def->ext) return NULL;
	char *abs = absolute(path), *root = abs ? project(abs, def->roots) : NULL, *u = abs ? uri(abs) : NULL;
	LspDoc *d = calloc(1, sizeof *d);
	free(abs);
	if (!d || !root || !u) { free(d); free(root); free(u); return NULL; }
	Srv *s = srvs;
	while (s && (s->def->argv != def->argv || s->closing || strcmp(s->root, root))) s = s->link;
	if (!s) {
		if (!(s = calloc(1, sizeof *s))) { free(root); free(d); free(u); return NULL; }
		s->def = def; s->root = root;
		start(s);
		if (s->proc < 0) { free(root); free(s); free(d); free(u); return NULL; }
		s->link = srvs; srvs = s;
	} else free(root);
	d->s = s; d->uri = u; d->t = t;
	d->link = s->docs; s->docs = d;
	open_doc(d);
	return d;
}

/* A server that is gone and has no files: off the list, freed. */
static void drop(Srv *s)
{
	Srv **q = &srvs;
	while (*q != s) q = &(*q)->link;
	*q = s->link;
	free(s->root); free(s->in.s); free(s->held.s); free(s->init.s); free(s);
}

void lsp_close(LspDoc *d)
{
	if (!d) return;
	Srv *s = d->s;
	begin("textDocument/didClose");
	str_addf(&o, "{\"textDocument\":{\"uri\":\"%s\"}}", d->uri);
	end(s);
	LspDoc **p = &s->docs;
	while (*p != d) p = &(*p)->link;
	*p = d->link;
	forget(d);
	free(d->diag.v); free(d->prog.v); free(d->chg.s); free(d->log); free(d->uri); free(d);
	if (s->docs) return;
	/* the last file of the project: the server goes (it is freed once it
	   has exited, since the loop still calls lsp_out until then) */
	s->closing = 1;
	if (s->proc < 0) { drop(s); return; }
	o.n = 0;
	str_addf(&o, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"method\":\"shutdown\"}", s->next++);
	send(s);
	o.n = 0;
	str_lit(&o, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}");
	send(s);
	proc_close(s->proc);
}

void lsp_edit(LspDoc *d, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	if (!d) return;
	d->t = t;
	shift_set(&d->diag, pos, ndel, nins);
	shift_set(&d->prog, pos, ndel, nins);
	Ed e = { d->ver, { 0, 0 }, { 0, 0 }, { 0, 0 } };
	where(t, pos, &e.s.l, &e.s.c);
	where(t, pos + ndel, &e.e.l, &e.e.c);
	size_t k = nins;
	while (k > 0 && ins[k - 1] != '\n') k--;
	e.i.l = e.s.l;
	for (size_t j = 0; j < k; j++) e.i.l += ins[j] == '\n';
	e.i.c = (k ? 0 : e.s.c) + lsp_u16((const unsigned char *)ins + k, nins - k);
	if (!d->log && !(d->log = malloc(LOG * sizeof *d->log))) d->nlog = LOG;   /* no log: nothing older maps */
	if (d->nlog == LOG) {                   /* full: the older half goes */
		if (d->log) memmove(d->log, d->log + LOG / 2, LOG / 2 * sizeof *d->log);
		d->nlog = d->log ? LOG / 2 : 0;
		d->floor = d->nlog ? d->log[0].v + 1 : d->ver + 1;
	}
	if (d->log) d->log[d->nlog++] = e;
	Str *c = &d->chg;
	Str keep = o;                   /* writes go to o */
	o = *c;
	if (o.n) str_lit(&o, ",");
	str_addf(&o, "{\"range\":{\"start\":{\"line\":%zu,\"character\":%zu},\"end\":{\"line\":%zu,\"character\":%zu}},\"text\":\"",
	         e.s.l, e.s.c, e.e.l, e.e.c);
	/* U+FFFD for a byte that is not UTF-8 depends on its neighbours, so
	   once the text holds one a change cannot be sent alone: from then on
	   the whole text goes (lsp_flush) ... */
	if (str_addq(&o, ins, nins)) d->bad = 1;
	str_lit(&o, "\"}");
	*c = o;
	o = keep;
	/* nor do its columns move by the edit alone (two bytes may become one
	   character), so nothing older maps across it: it is shown no more */
	if (d->bad) { d->nlog = 0; d->floor = d->ver + 1; }
}

void lsp_flush(LspDoc *d)
{
	if (!d || !d->chg.n) return;
	d->ver++;
	begin("textDocument/didChange");
	str_addf(&o, "{\"textDocument\":{\"uri\":\"%s\",\"version\":%ld},\"contentChanges\":[", d->uri, d->ver);
	if (d->bad) {
		str_lit(&o, "{\"text\":\"");
		put_text(d->t);
		str_lit(&o, "\"}");
	} else str_add(&o, d->chg.s, d->chg.n);
	str_lit(&o, "]}");
	d->chg.n = 0;
	end(d->s);
}

void lsp_saved(LspDoc *d)
{
	if (!d) return;
	lsp_flush(d);
	begin("textDocument/didSave");
	str_addf(&o, "{\"textDocument\":{\"uri\":\"%s\"}}", d->uri);
	end(d->s);
}

void lsp_restart(LspDoc *d)
{
	if (!d) return;
	Srv *s = d->s;
	lsp_flush(d);
	if (s->proc < 0) {
		start(s);
		for (LspDoc *e = s->docs; e; e = e->link) reopen(e);
		return;
	}
	for (int k = 0; k < LSP_NASK; k++) cancel(d, k);
	begin("textDocument/didClose");
	str_addf(&o, "{\"textDocument\":{\"uri\":\"%s\"}}", d->uri);
	end(s);
	reopen(d);
}

const LspDiag *lsp_diags(const LspDoc *d, int *n) { *n = d ? d->diag.n : 0; return d ? d->diag.v : NULL; }
const LspDiag *lsp_progress(const LspDoc *d, int *n) { *n = d ? d->prog.n : 0; return d ? d->prog.v : NULL; }

static int current(const LspDoc *d, long ver) { return ver == d->ver && !d->chg.n; }   /* no edit since ver */

void lsp_ask(LspDoc *d, int kind, size_t pos)
{
	static const char *const method[] = { "textDocument/hover", "$/lean/plainGoal" };
	if (!d || d->s->proc < 0) return;
	lsp_flush(d);
	cancel(d, kind);
	free(d->ask[kind].ans);
	d->ask[kind].ans = NULL;
	d->ask[kind].id = d->s->next++;
	d->ask[kind].ver = d->ver;
	o.n = 0;
	str_addf(&o, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"method\":\"%s\",\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":",
	         d->ask[kind].id, method[kind], d->uri);
	putpos(d->t, pos);
	str_lit(&o, "}}");
	send(d->s);
}

/* A reply is shown only if the text has not changed since the request
   (LspSync: mapping it from the request's version is wrong, the server may
   have answered on a newer one). */
const char *lsp_answer(const LspDoc *d, int kind)
{
	return d && current(d, d->ask[kind].ver) ? d->ask[kind].ans : NULL;
}

/* --- receiving ------------------------------------------------------------- */

static char *str(const char *m, int i)
{
	if (i < 0 || tk[i].type != 's') return NULL;
	char *b = malloc(tk[i].z - tk[i].a + 1);
	if (b) json_str(m, tk, i, b);
	return b;
}

static Pos pos(const char *m, int p)
{
	long l = json_int(m, tk, json_get(m, tk, p, "line")), c = json_int(m, tk, json_get(m, tk, p, "character"));
	return (Pos){ l < 0 ? (size_t)-1 : (size_t)l, c < 0 ? 0 : (size_t)c };
}

static int before(Pos a, Pos b) { return a.l < b.l || (a.l == b.l && a.c < b.c); }

/* Position p of an older version, after edit e (Neovim's extmarks, as
   lsp.h says): a start in the deleted text goes after the inserted text,
   an end to the start of the edit. */
static Pos after(Pos p, const Ed *e, int end)
{
	if (end ? !before(e->s, p) : before(p, e->s)) return p;
	if (end ? !before(e->e, p) : before(p, e->e)) return end ? e->s : e->i;
	return p.l == e->e.l ? (Pos){ e->i.l, e->i.c + (p.c - e->e.c) } : (Pos){ p.l - e->e.l + e->i.l, p.c };
}

/* Byte in d's text of position p of version v. */
static size_t byte(const LspDoc *d, Pos p, long v, int end)
{
	for (int k = 0; k < d->nlog; k++) if (d->log[k].v >= v) p = after(p, &d->log[k], end);
	const Text *t = d->t;
	if (p.l > text_lines(t)) return text_len(t);
	size_t a = text_line_start(t, p.l);
	size_t n = (p.l < text_lines(t) ? text_line_start(t, p.l + 1) - 1 : text_len(t)) - a;
	const unsigned char *b = bytes(t, a, &n);
	return a + lsp_u16_byte(b, n, p.c);
}

static int cmp(const void *x, const void *y)
{
	const LspDiag *a = x, *b = y;
	return (a->a > b->a) - (a->a < b->a);
}

/* A set of ranges for version ver of d from array arr: replaces v, or
   with add (Lean's incremental diagnostics) extends it.  The server goes
   through the versions in order, so nothing older than ver comes again:
   the log is pruned up to it. */
static void ranges(LspDoc *d, Set *v, long ver, const char *m, int arr, int add)
{
	if (ver < d->floor) return;
	if (!add || ver != v->ver) clear(v);
	v->ver = ver;
	int k = 0;
	while (k < d->nlog && d->log[k].v < ver) k++;
	if (k) memmove(d->log, d->log + k, (size_t)(d->nlog - k) * sizeof *d->log);
	d->nlog -= k;
	d->floor = ver;
	for (int k = 0, e; (e = json_at(tk, arr, k)) >= 0; k++) {
		int r = json_get(m, tk, e, "range");
		if (v->n == v->cap) {
			int c = v->cap ? 2 * v->cap : 16;
			LspDiag *n = realloc(v->v, (size_t)c * sizeof *n);
			if (!n) break;
			v->v = n; v->cap = c;
		}
		Pos a = pos(m, json_get(m, tk, r, "start")), z = pos(m, json_get(m, tk, r, "end"));
		int point = !before(a, z);
		LspDiag g = { byte(d, a, ver, 0), byte(d, point ? a : z, ver, !point),
		              (int)json_int(m, tk, json_get(m, tk, e, "severity")), NULL };
		if (!point && g.a >= g.z) continue;     /* all of it deleted since */
		if (g.sev < 1) g.sev = 1;
		g.msg = str(m, json_get(m, tk, e, "message"));
		v->v[v->n++] = g;
	}
	if (v->n > 1) qsort(v->v, (size_t)v->n, sizeof *v->v, cmp);
	core_changed = 1;
}

static LspDoc *doc(Srv *s, const char *m, int uri)
{
	for (LspDoc *d = s->docs; d; d = d->link) {
		size_t l = strlen(d->uri);
		if (uri >= 0 && tk[uri].type == 's' && tk[uri].z - tk[uri].a == l && !memcmp(m + tk[uri].a, d->uri, l)) return d;
	}
	return NULL;
}

static void handle(Srv *s, const char *m, size_t n)
{
	if (json_parse(m, n, &tk, &ntk) < 0) return;
	int id = json_get(m, tk, 0, "id"), meth = json_get(m, tk, 0, "method"), p = json_get(m, tk, 0, "params");
	if (meth >= 0 && id >= 0) {                 /* a request from the server */
		o.n = 0;
		str_lit(&o, "{\"jsonrpc\":\"2.0\",\"id\":");
		str_add(&o, m + tk[id].a - (tk[id].type == 's'), tk[id].z - tk[id].a + 2 * (tk[id].type == 's'));
		str_lit(&o, ",\"result\":null}");
		send(s);
		return;
	}
	if (meth >= 0) {
		if (lsp_tap && s->def->lean) lsp_tap(0, m, n);
		char name[64];
		if (tk[meth].type != 's' || tk[meth].z - tk[meth].a >= sizeof name) return;
		json_str(m, tk, meth, name);
		if (!strcmp(name, "textDocument/publishDiagnostics")) {
			LspDoc *d = doc(s, m, json_get(m, tk, p, "uri"));
			long v = json_int(m, tk, json_get(m, tk, p, "version"));
			/* without a version: taken as for the text sent last, as
			   Neovim does (Lean, clangd and rust-analyzer all send it) */
			if (d) ranges(d, &d->diag, v < 0 ? d->ver : v, m, json_get(m, tk, p, "diagnostics"),
			              json_true(m, tk, json_get(m, tk, p, "isIncremental")));
		} else if (!strcmp(name, "$/lean/fileProgress")) {
			int td = json_get(m, tk, p, "textDocument");
			LspDoc *d = doc(s, m, json_get(m, tk, td, "uri"));
			long v = json_int(m, tk, json_get(m, tk, td, "version"));
			if (d && v >= 0) ranges(d, &d->prog, v, m, json_get(m, tk, p, "processing"), 0);
		} else if (!strcmp(name, "$/progress")) {        /* lualine's lsp_status counts the tokens */
			int k = json_get(m, tk, json_get(m, tk, p, "value"), "kind");
			size_t z = k >= 0 ? tk[k].z - tk[k].a : 0;
			if (z == 5 && !memcmp(m + tk[k].a, "begin", 5)) s->work = (s->work < 0 ? 0 : s->work) + 1;
			else if (z == 3 && !memcmp(m + tk[k].a, "end", 3) && s->work > 0) s->work--;
			else return;
			core_changed = 1;
		}
		return;
	}
	long rid = json_int(m, tk, id);
	for (Raw **q = &raws, *r; (r = *q); q = &r->link)
		if (r->s == s && r->id == rid) {
			*q = r->link;
			r->fn(r->ctx, m, n);
			free(r);
			return;
		}
	if (rid == 0) {                              /* initialize answered */
		if (s->def->lean) { s->init.n = 0; str_add(&s->init, m, n); }
		if (lsp_tap && s->def->lean) lsp_tap(0, m, n);
		s->ready = 1;
		begin("initialized");
		str_lit(&o, "{}");
		end(s);
		if (s->held.n) { proc_write(s->proc, s->held.s, s->held.n); s->held.n = 0; }
		return;
	}
	for (LspDoc *d = s->docs; d; d = d->link)
		for (int k = 0; k < LSP_NASK; k++) {
			if (d->ask[k].id != rid) continue;
			d->ask[k].id = 0;
			int r = json_get(m, tk, 0, "result");
			if (r < 0) return;                  /* an error: cancelled */
			int c = k == LSP_GOAL ? json_get(m, tk, r, "rendered") : json_get(m, tk, r, "contents");
			if (c >= 0 && tk[c].type == 'o') c = json_get(m, tk, c, "value");      /* MarkupContent */
			if (c >= 0 && tk[c].type == 'a') c = json_at(tk, c, 0);                 /* MarkedString[] */
			if (c >= 0 && tk[c].type == 'o') c = json_get(m, tk, c, "value");      /* {language, value} */
			d->ask[k].ans = c >= 0 && tk[c].type == 's' ? str(m, c) : strdup("");
			core_changed = 1;
			return;
		}
}

static void dead(Srv *s)
{
	s->proc = -1;
	s->init.n = 0;
	for (Raw **q = &raws, *r; (r = *q); )
		if (r->s == s) { *q = r->link; r->fn(r->ctx, NULL, 0); free(r); }
		else q = &r->link;
	for (LspDoc *d = s->docs; d; d = d->link) forget(d);
	if (s->closing) drop(s);
}

static void out(void *ctx, int stream, const char *b, size_t n)
{
	Srv *s = ctx;
	if (stream == 0) { dead(s); return; }
	if (stream != 1 || !n || str_add(&s->in, b, n)) return;
	for (;;) {
		char *e = strstr(s->in.s, "\r\n\r\n");   /* headers are text: no NUL before it */
		if (!e) return;
		size_t h = (size_t)(e - s->in.s) + 4, len = 0;
		int ok = 0;
		for (char *l = s->in.s; l < e; l = strstr(l, "\r\n") + 2)
			if (!strncasecmp(l, "Content-Length:", 15)) { len = strtoul(l + 15, NULL, 10); ok = 1; }
		if (!ok) { s->in.n = 0; return; }        /* not LSP: drop what came */
		if (s->in.n - h < len) return;
		handle(s, s->in.s + h, len);
		memmove(s->in.s, s->in.s + h + len, s->in.n - h - len);
		s->in.n -= h + len;
		s->in.s[s->in.n] = 0;
	}
}

const char *lsp_name(const LspDoc *d, int *work)
{
	*work = d ? d->s->work : -1;
	if (!d) return NULL;
	const char *a = d->s->def->argv[0], *b = strrchr(a, '/');
	return b ? b + 1 : a;
}

const char *lsp_uri(const LspDoc *d) { return d ? d->uri : NULL; }
const char *lsp_init(const LspDoc *d, size_t *n) { *n = d ? d->s->init.n : 0; return *n ? d->s->init.s : NULL; }

int lsp_send(const char *u, const char *method, const char *params, size_t n, LspRawFn fn, void *ctx)
{
	LspDoc *d = NULL;
	for (Srv *s = srvs; s && !d; s = s->link)
		for (d = s->docs; d && strcmp(d->uri, u); d = d->link) ;
	Raw *r = fn ? malloc(sizeof *r) : NULL;
	if (!d || d->s->proc < 0 || (fn && !r)) { free(r); return -1; }
	lsp_flush(d);
	o.n = 0;
	str_lit(&o, "{\"jsonrpc\":\"2.0\",");
	if (r) {
		*r = (Raw){ d->s, d->s->next++, fn, ctx, raws };
		raws = r;
		str_addf(&o, "\"id\":%ld,", r->id);
	}
	str_lit(&o, "\"method\":\"");
	str_addq(&o, method, strlen(method));
	str_lit(&o, "\",\"params\":");
	str_add(&o, params, n);
	str_lit(&o, "}");
	send(d->s);
	return 0;
}
