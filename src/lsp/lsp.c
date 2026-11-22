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
#include <stdint.h>
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
	Set diag, prog, done;   /* done: Lean's goals accomplished (its silent diagnostics) */
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

/* Text and one FACE_* per byte. */
typedef struct { Str t, f; } Out;

/* What it shows: the cursor (pos) of the last current window on a Lean
   document d, at d's version ver.  One question at a time goes to Lean;
   want: what is shown is owed a new one.  sess is Lean's RPC session for d
   (lean.nvim's rpc.open), its JSON token as Lean sent it.  The last
   answers: ng goals (-1: none at all), drawn in goals, and the expected
   type in term; all is what the window holds, next the text that may
   replace it.  again: the session was renewed once for this question. */
static struct {
	Editor *e;
	int nr, off, busy, want, again, shown, ng;
	LspDoc *d;
	size_t pos;
	long ver;
	Str sess;
	Out goals, term, all, next;
} G;
static Str q;                   /* a question's params */

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

static void putpos(Str *s, const Text *t, size_t pos)
{
	size_t l, c;
	where(t, pos, &l, &c);
	str_addf(s, "{\"line\":%zu,\"character\":%zu}", l, c);
}

/* JSON token x of m as it stands, quotes too */
static void putraw(Str *s, const char *m, int x)
{
	str_add(s, m + tk[x].a - (tk[x].type == 's'), tk[x].z - tk[x].a + 2 * (tk[x].type == 's'));
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
	        "\"window\":{\"workDoneProgress\":true},\"general\":{\"positionEncodings\":[\"utf-16\"]},\"lean\":{\"incrementalDiagnosticSupport\":true,\"silentDiagnosticSupport\":true}}");
	if (s->def->lean) str_lit(&o, ",\"initializationOptions\":{\"hasWidgets\":true}");
	str_lit(&o, "}}");
	frame(s, 1);
}

/* --- what is shown ---------------------------------------------------------- */

static void gpoke(const LspDoc *d);

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
	clear(&d->done);
	d->prog.ver = -1;                   /* elaborated anew: no word on it yet */
	if (d == G.d) G.shown = 0, G.sess.n = 0;    /* a new server: its own RPC sessions */
	for (int k = 0; k < LSP_NASK; k++) { free(d->ask[k].ans); d->ask[k].ans = NULL; d->ask[k].id = 0; }
	core_changed = 1;
	gpoke(d);
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
static size_t shift_end(size_t z, size_t pos, size_t ndel, size_t nins)
{
	return z <= pos ? z : z <= pos + ndel ? pos : z - ndel + nins;
}

static int shift(LspDiag *g, size_t pos, size_t ndel, size_t nins)
{
	size_t a = g->a < pos ? g->a : (g->a > pos + ndel ? g->a : pos + ndel) - ndel + nins;
	size_t z = shift_end(g->z, pos, ndel, nins), fz = shift_end(g->fz, pos, ndel, nins);
	if (g->a == g->z) z = a;
	else if (a >= z) return 0;
	g->a = a; g->z = z; g->fz = fz < z ? z : fz;
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
	if (d) d->prog.ver = -1;                 /* no $/lean/fileProgress yet */
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
	if (G.d == d) G.d = NULL;
	free(d->diag.v); free(d->prog.v); free(d->done.v); free(d->chg.s); free(d->log); free(d->uri); free(d);
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
	shift_set(&d->done, pos, ndel, nins);
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
	if (!d || d->s->proc < 0) return;
	lsp_flush(d);
	cancel(d, kind);
	free(d->ask[kind].ans);
	d->ask[kind].ans = NULL;
	d->ask[kind].id = d->s->next++;
	d->ask[kind].ver = d->ver;
	o.n = 0;
	str_addf(&o, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"%s\"},\"position\":",
	         d->ask[kind].id, d->uri);
	putpos(&o, d->t, pos);
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

/* Whether token i is the string (or number) w. */
static int is(const char *m, int i, const char *w)
{
	return i >= 0 && tk[i].z - tk[i].a == strlen(w) && !memcmp(m + tk[i].a, w, tk[i].z - tk[i].a);
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
static size_t byte(const LspDoc *d, Pos *p, long v, int end)
{
	for (int k = 0; k < d->nlog; k++) if (d->log[k].v >= v) *p = after(*p, &d->log[k], end);
	const Text *t = d->t;
	if (p->l > text_lines(t)) return text_len(t);
	size_t a = text_line_start(t, p->l);
	size_t n = (p->l < text_lines(t) ? text_line_start(t, p->l + 1) - 1 : text_len(t)) - a;
	const unsigned char *b = bytes(t, a, &n);
	return a + lsp_u16_byte(b, n, p->c);
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
		if (json_true(m, tk, json_get(m, tk, e, "isSilent")) != (v == &d->done)) continue;
		if (v->n == v->cap) {
			int c = v->cap ? 2 * v->cap : 16;
			LspDiag *n = realloc(v->v, (size_t)c * sizeof *n);
			if (!n) break;
			v->v = n; v->cap = c;
		}
		Pos a = pos(m, json_get(m, tk, r, "start")), z = pos(m, json_get(m, tk, r, "end"));
		int point = !before(a, z), f = json_get(m, tk, json_get(m, tk, e, "fullRange"), "end");
		LspDiag g = { byte(d, &a, ver, 0), 0, 0, (int)json_int(m, tk, json_get(m, tk, e, "severity")), NULL };
		g.z = point ? g.a : byte(d, &z, ver, 1);
		/* all of it deleted since; a range past its line's end only
		   becomes a point there (LSP: a column past the end is the end) */
		if (!point && !before(a, z)) continue;
		Pos fp = pos(m, f);
		g.fz = f < 0 ? g.z : byte(d, &fp, ver, 1);
		if (g.fz < g.z) g.fz = g.z;
		if (g.sev < 1 || g.sev > 4) g.sev = 1;
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
		putraw(&o, m, id);
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
			for (int k = 0; d && k < 2; k++)
				ranges(d, k ? &d->done : &d->diag, v < 0 ? d->ver : v, m, json_get(m, tk, p, "diagnostics"),
				       json_true(m, tk, json_get(m, tk, p, "isIncremental")));
			if (d) gpoke(d);
		} else if (!strcmp(name, "$/lean/fileProgress")) {
			int td = json_get(m, tk, p, "textDocument");
			LspDoc *d = doc(s, m, json_get(m, tk, td, "uri"));
			long v = json_int(m, tk, json_get(m, tk, td, "version"));
			if (d && v >= 0) { ranges(d, &d->prog, v, m, json_get(m, tk, p, "processing"), 0); gpoke(d); }
		} else if (!strcmp(name, "$/progress")) {        /* lualine's lsp_status counts the tokens */
			int k = json_get(m, tk, json_get(m, tk, p, "value"), "kind");
			if (is(m, k, "begin")) s->work = (s->work < 0 ? 0 : s->work) + 1;
			else if (is(m, k, "end") && s->work > 0) s->work--;
			else return;
			core_changed = 1;
		}
		return;
	}
	long rid = json_int(m, tk, id);
	for (Raw **at = &raws, *r; (r = *at); at = &r->link)
		if (r->s == s && r->id == rid) {
			*at = r->link;
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
			int c = json_get(m, tk, r, "contents");
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
	for (Raw **at = &raws, *r; (r = *at); )
		if (r->s == s) { *at = r->link; r->fn(r->ctx, NULL, 0); free(r); }
		else at = &r->link;
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

/* --- the goals window (lean.nvim's infoview as text, lsp.h) ---------------- */

static void put(Out *o, const char *p, size_t n, unsigned char f)
{
	str_add(&o->t, p, n);
	while (n--) str_add(&o->f, (const char *)&f, 1);
}

static void putz(Out *o, const char *p, unsigned char f) { put(o, p, strlen(p), f); }

static void putj(Out *o, const char *m, int i, unsigned char f)
{
	char *s = str(m, i);
	if (s) putz(o, s, f);
	free(s);
}

/* blocks are one blank line apart (lean.nvim's Element:concat) */
static void block(Out *o) { if (o->t.n) put(o, "\n\n", 2, FACE_NONE); }

static void range(Out *o, size_t a, size_t z, const char *fmt, unsigned char f)
{
	size_t l, c, k, d;
	where(G.d->t, a, &l, &c);
	where(G.d->t, z, &k, &d);
	q.n = 0;
	str_addf(&q, fmt, l + 1, c + 1, k + 1, d + 1);    /* std.lsp's range_to_string */
	put(o, q.s, q.n, f);
}

/* Lean's TaggedText, with lean.nvim's colours for its diff marks */
static void tagged(Out *o, const char *m, int t, unsigned char f)
{
	static const char *const tag[] = { "wasChanged", "willChange", "wasInserted", "willInsert", "wasDeleted", "willDelete" };
	static const unsigned char face[] = { FACE_DIFF_DELTA, FACE_DIFF_MINUS, FACE_DIFF_PLUS, FACE_DIFF_PLUS, FACE_DIFF_MINUS, FACE_DIFF_MINUS };
	int x;
	if ((x = json_get(m, tk, t, "text")) >= 0) putj(o, m, x, f);
	else if ((x = json_get(m, tk, t, "append")) >= 0)
		for (int k = 0, y; (y = json_at(tk, x, k)) >= 0; k++) tagged(o, m, y, f);
	else if ((x = json_get(m, tk, t, "tag")) >= 0) {
		int s = json_get(m, tk, json_at(tk, x, 0), "diffStatus");
		for (int k = 0; k < 6; k++) if (is(m, s, tag[k])) f = face[k];
		tagged(o, m, json_at(tk, x, 1), f);
	}
}

static unsigned char marked(const char *m, int g, unsigned char f)
{
	return json_true(m, tk, json_get(m, tk, g, "isInserted")) ? FACE_DIFF_PLUS
	     : json_true(m, tk, json_get(m, tk, g, "isRemoved")) ? FACE_DIFF_MINUS : f;
}

/* An InteractiveGoal as lean.nvim's interactive_goal.Goal draws it */
static void goal(Out *o, const char *m, int g)
{
	int x = json_get(m, tk, g, "userName");
	if (x >= 0) { putz(o, "case ", FACE_KEYWORD); putj(o, m, x, FACE_NONE); putz(o, "\n", FACE_NONE); }
	for (int k = 0, h; (h = json_at(tk, json_get(m, tk, g, "hyps"), k)) >= 0; k++) {
		int nm = json_get(m, tk, h, "names");
		if (json_at(tk, nm, 0) < 0) continue;
		for (int j = 0, y; (y = json_at(tk, nm, j)) >= 0; j++) {
			if (j) putz(o, " ", FACE_NONE);
			char *s = str(m, y);       /* ✝: inaccessible, lean.nvim's Comment */
			if (s) putz(o, s, marked(m, h, strstr(s, "\xE2\x9C\x9D") ? FACE_COMMENT : FACE_TYPE));
			free(s);
		}
		putz(o, " : ", FACE_NONE);
		tagged(o, m, json_get(m, tk, h, "type"), FACE_NONE);
		if ((x = json_get(m, tk, h, "val")) >= 0) { putz(o, " := ", FACE_NONE); tagged(o, m, x, FACE_NONE); }
		putz(o, "\n", FACE_NONE);
	}
	x = json_get(m, tk, g, "goalPrefix");
	if (x >= 0) putj(o, m, x, marked(m, g, FACE_OPERATOR));
	else putz(o, "\xE2\x8A\xA2 ", marked(m, g, FACE_OPERATOR));          /* ⊢ */
	tagged(o, m, json_get(m, tk, g, "type"), FACE_NONE);
}

static Buf *gbuf(void) { return G.e && G.nr ? core_buf_nr(G.e, G.nr) : NULL; }

static void gfaces(const Buf *b, size_t a, size_t z, unsigned char *f)
{
	(void)b;
	for (size_t i = a; i < z; i++) f[i - a] = i < G.all.f.n ? (unsigned char)G.all.f.s[i] : FACE_NONE;
}

static void cat(Out *o, const Out *p) { block(o); str_add(&o->t, p->t.s, p->t.n); str_add(&o->f, p->f.s, p->f.n); }

/* Whether set v has a range over the cursor's line (lean.nvim's
   progress.at and goals_accomplished_at). */
static int over(const Set *v)
{
	size_t line = text_line_of(G.d->t, G.pos);
	for (int k = 0; k < v->n; k++)
		if (text_line_of(G.d->t, v->v[k].a) <= line && line <= text_line_of(G.d->t, v->v[k].z)) return 1;
	return 0;
}

/* The window gets what lean.nvim's components draw, in its order: the
   goals (wrap_goals' title), the expected type, the messages that start
   on the cursor line (diagnostics_at) with their full range; or its words
   while Lean has said nothing here yet, or once the server is dead. */
static void commit(void)
{
	static const char *const sev[] = { "error", "warning", "information", "hint" };
	Buf *b = gbuf();
	if (!b || !G.d) return;
	Out *o = &G.next;
	o->t.n = o->f.n = 0;
	if (G.d->s->proc < 0) putz(o, "\xF0\x9F\xAA\xA6 The Lean language server is dead.", FACE_NONE);   /* 🪦 */
	else if (!G.shown) putz(o, "Processing file...", FACE_NONE);
	else {
		if (over(&G.d->done)) putz(o, "Goals accomplished \xF0\x9F\x8E\x89", FACE_MARKUP_HEADING);   /* 🎉 */
		else if (!G.ng) putz(o, "No goals.", FACE_MARKUP_HEADING);
		if (G.goals.t.n) cat(o, &G.goals);
		if (G.term.t.n) cat(o, &G.term);
		size_t line = text_line_of(G.d->t, G.pos);
		for (int k = 0; k < G.d->diag.n; k++) {
			const LspDiag *g = &G.d->diag.v[k];
			unsigned char f = (unsigned char)(FACE_DIAG_ERROR + g->sev - 1);
			if (text_line_of(G.d->t, g->a) != line) continue;
			block(o);
			putz(o, "\xE2\x96\xBC ", f);                  /* ▼ */
			range(o, g->a, g->fz, "%zu:%zu-%zu:%zu: ", f);
			putz(o, sev[g->sev - 1], f);
			putz(o, ":\n", f);
			if (g->msg) putz(o, g->msg, FACE_NONE);
		}
	}
	put(o, "\n", 1, FACE_NONE);              /* core's lines end in one: the last is drawn */
	if (o->t.n == G.all.t.n && !memcmp(o->t.s, G.all.t.s, o->t.n) && !memcmp(o->f.s, G.all.f.s, o->f.n)) return;
	Out t = G.all;
	G.all = *o;
	*o = t;
	core_buf_fill(G.e, b, G.all.t.s, G.all.t.n, SIZE_MAX);
}

static void on_sess(void *ctx, const char *m, size_t n);
static void on_goals(void *ctx, const char *m, size_t n);
static void on_term(void *ctx, const char *m, size_t n);

/* Asks fn's question about the cursor: Lean's RPC session, or a call in it */
static void gsend(const char *method, LspRawFn fn)
{
	q.n = 0;
	if (fn == on_sess) str_addf(&q, "{\"uri\":\"%s\"}", G.d->uri);
	else {
		str_lit(&q, "{\"sessionId\":");
		str_add(&q, G.sess.s, G.sess.n);
		str_addf(&q, ",\"method\":\"%s\",", method);
		for (int k = 0; k < 2; k++) {       /* Lean wants both: the call's and its method's */
			str_addf(&q, "\"textDocument\":{\"uri\":\"%s\"},\"position\":", G.d->uri);
			putpos(&q, G.d->t, G.pos);
			str_addz(&q, k ? "}}" : ",\"params\":{");
		}
	}
	G.busy = !lsp_send(G.d->uri, fn == on_sess ? "$/lean/rpc/connect" : "$/lean/rpc/call", q.s, q.n, fn, G.d);
}

static void gask(void)
{
	if (!G.d || !G.want || G.busy || !gbuf()) return;
	if (G.d->s->proc < 0) { commit(); return; }
	if (over(&G.d->prog) || G.d->prog.ver < 0) { if (!G.shown) commit(); return; }   /* elaborating here (or no word yet) */
	G.want = 0;
	if (!G.sess.n) gsend(NULL, on_sess);
	else gsend("Lean.Widget.getInteractiveGoals", on_goals);
}

/* What d's server said changed: shown again. */
static void gpoke(const LspDoc *d)
{
	if (d == G.d) { commit(); gask(); }
}

/* Lean gave no answer here: what was shown for elsewhere goes (the
   expected type alone when the goals here were answered) */
static void none(int term)
{
	if (G.want || !G.d) return;
	G.term.t.n = G.term.f.n = 0;
	if (!term) G.goals.t.n = G.goals.f.n = 0, G.ng = -1;
	G.shown = 1;
	commit();
}

/* An answer that came too late (the cursor or the text moved, the server
   died) or from an outdated session (Lean's -32900, after it elaborated the
   file anew): ask again, after -32900 once a question, so a server that
   keeps refusing is not asked in a loop. */
static int late(const char *m, int term)
{
	G.busy = 0;
	int e = m ? json_get(m, tk, 0, "error") : 0;   /* no answer at all: none */
	if (!m || is(m, json_get(m, tk, e, "code"), "-32900")) { G.sess.n = 0; G.want |= !G.again; G.again = 1; }
	if (e >= 0) none(term);
	if (G.want || !G.d || e >= 0) { gask(); return 1; }
	return 0;
}

static void on_sess(void *ctx, const char *m, size_t n)
{
	(void)n;
	int x = m ? json_get(m, tk, json_get(m, tk, 0, "result"), "sessionId") : -1;
	G.busy = 0;
	/* refused: asked again only once the cursor or the text moves */
	if (x >= 0 && ctx == G.d) { G.sess.n = 0; putraw(&G.sess, m, x); G.want = 1; }
	else if (m && ctx == G.d) none(0);
	gask();
}

/* lean.nvim's components.goal_at and its wrap_goals */
static void on_goals(void *ctx, const char *m, size_t n)
{
	(void)ctx; (void)n;
	if (late(m, 0)) return;
	int gs = json_get(m, tk, json_get(m, tk, 0, "result"), "goals");
	Out *o = &G.goals;
	o->t.n = o->f.n = 0;
	G.ng = gs >= 0 && tk[gs].type == 'a' ? tk[gs].n : -1;
	if (G.ng > 1) { q.n = 0; str_addf(&q, "\xE2\x96\xBC %d goals\n", G.ng); put(o, q.s, q.n, FACE_DIAG_HINT); }
	for (int k = 0; k < G.ng; k++) { if (k) put(o, "\n\n", 2, FACE_NONE); goal(o, m, json_at(tk, gs, k)); }
	gsend("Lean.Widget.getInteractiveTermGoal", on_term);
}

/* lean.nvim's interactive_term_goal */
static void on_term(void *ctx, const char *m, size_t n)
{
	(void)ctx; (void)n;
	if (late(m, 1)) return;
	int r = json_get(m, tk, 0, "result"), x = json_get(m, tk, r, "range");
	G.term.t.n = G.term.f.n = 0;
	if (x >= 0) {
		Pos a = pos(m, json_get(m, tk, x, "start")), z = pos(m, json_get(m, tk, x, "end"));
		q.n = 0;
		str_addf(&q, "\xE2\x96\xBC expected type (%zu:%zu-%zu:%zu)\n", a.l + 1, a.c + 1, z.l + 1, z.c + 1);
		put(&G.term, q.s, q.n, FACE_PUNCTUATION_SPECIAL);
		goal(&G.term, m, r);
	}
	G.shown = 1;
	commit();
}

void lsp_goals(Editor *e)
{
	G.e = e;
	LspDoc *d = e->buf->lsp;
	if (!d || !d->s->def->lean) return;
	int fresh = !G.off && !gbuf();
	if (fresh) {
		int v = 2 * e->cols > 5 * e->rows;      /* lean.nvim: wider than 2.5 times as tall */
		if (core_new(e, v)) return;
		core_resize(e, v, v ? e->cols / 3 : e->rows / 3, 1);
		G.nr = e->buf->nr;
		e->buf->nonumber = 1;
		e->buf->faces = gfaces;
		core_win_goto(e, 'p', 1);
	}
	size_t pos = core_line_start(e, e->win->line) + e->win->col;
	if (d != G.d) G.sess.n = G.shown = 0;
	if (d != G.d || pos != G.pos || d->ver != G.ver) { G.d = d; G.pos = pos; G.ver = d->ver; G.want = 1; G.again = 0; }
	if (fresh) { G.all.t.n = 0; commit(); }   /* the new buffer holds nothing yet */
	gask();
}

/* Closes the window if one shows it (its buffer goes too), else opens it */
void lsp_goals_toggle(Editor *e)
{
	G.e = e;
	Buf *b = gbuf();
	G.off = 0;
	for (Win *w = e->wins; b && w; w = w->next) G.off |= w->buf == b;
	if (b) core_buf_delete(e, b->nr, 1);
	G.nr = 0;
	if (!G.off) lsp_goals(e);
}
