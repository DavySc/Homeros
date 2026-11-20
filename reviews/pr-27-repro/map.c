/* PR 27 review: the diagnostics mapping against an oracle that follows byte
   identities, not offsets.  Every byte has an id; a range is (id of its
   first byte, id of its last byte); an edit that deletes the first moves it
   to the byte after the deletion, one that deletes the last to the byte
   before it (lsp.h's rule).  Edits: inserts, deletes, replaces, joins,
   several per command, undo of the last command, with 1- to 4-byte
   characters.  Publications: for the current version, for one the editor
   has left, with unflushed edits pending, incremental.
   usage: map SEED TRIALS [burst] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"

int core_changed;
static ProcFn sfn; static void *sctx;
int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx) { (void)argv; (void)dir; sfn = fn; sctx = ctx; return 1; }
int proc_write(int id, const void *b, size_t n) { (void)id; (void)b; (void)n; return 0; }
void proc_close(int id) { (void)id; }

static unsigned long long rs;
static unsigned rnd(unsigned n) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33) % n; }

enum { N = 1 << 12, MAXE = 20000, MAXV = 20000, MAXD = 64 };
static unsigned char tx[N]; static int id[N]; static size_t n; static int nextid = 1;
typedef struct { size_t n; unsigned char *tx; int *id; } Snap;
static Snap before[MAXE];           /* the text before edit k */
static struct { size_t pos, ndel, nins; } ed[MAXE];
static int ned;
static Snap vs[MAXV]; static int vfirst[MAXV];   /* version v's text; the first edit after it */
static long ver;

static Snap snap(void)
{
	Snap s = { n, malloc(n + 1), malloc((n + 1) * sizeof(int)) };
	memcpy(s.tx, tx, n); memcpy(s.id, id, n * sizeof(int));
	return s;
}

typedef struct { int s, e, point; char msg[24]; int dead; } X;   /* s: -1 is the end of the text */
static X want[MAXD]; static int nwant;
static X old[MAXD]; static int nold;

static long idx(const int *ids, size_t m, int i) { for (size_t k = 0; k < m; k++) if (ids[k] == i) return (long)k; return -1; }

/* x through edit k */
static void follow(X *x, int k)
{
	if (x->dead) return;
	const Snap *b = &before[k];
	size_t pos = ed[k].pos, ndel = ed[k].ndel;
	if (x->s >= 0) {
		long i = idx(b->id, b->n, x->s);
		if (i < 0) { fprintf(stderr, "oracle lost a start\n"); exit(2); }
		if ((size_t)i >= pos && (size_t)i < pos + ndel) x->s = pos + ndel < b->n ? b->id[pos + ndel] : -1;
	}
	if (x->point) return;
	long j = idx(b->id, b->n, x->e);
	if ((size_t)j >= pos && (size_t)j < pos + ndel) { if (pos == 0) { x->dead = 1; return; } x->e = b->id[pos - 1]; }
	if (x->s < 0) { x->dead = 1; return; }
	/* order in the old text decides (both survive the edit or moved to survivors) */
	long i = idx(b->id, b->n, x->s);
	j = idx(b->id, b->n, x->e);
	if (i > j) x->dead = 1;
}

static LspDoc *d; static Text *t;

static void edit(size_t pos, size_t ndel, const unsigned char *ins, size_t nins)
{
	if (ned == MAXE || n - ndel + nins >= N) return;
	before[ned] = snap(); ed[ned].pos = pos; ed[ned].ndel = ndel; ed[ned].nins = nins;
	for (int i = 0; i < nwant; i++) follow(&want[i], ned);
	for (int i = 0; i < nold; i++) follow(&old[i], ned);
	ned++;
	lsp_edit(d, t, pos, ndel, (const char *)ins, nins);
	text_delete(t, pos, ndel); text_insert(t, pos, (const char *)ins, nins);
	memmove(tx + pos + nins, tx + pos + ndel, n - pos - ndel);
	memmove(id + pos + nins, id + pos + ndel, (n - pos - ndel) * sizeof(int));
	memcpy(tx + pos, ins, nins);
	for (size_t k = 0; k < nins; k++) id[pos + k] = nextid++;
	n = n - ndel + nins;
}

static size_t cstart(size_t i) { while (i > 0 && i < n && (tx[i] & 0xC0) == 0x80) i--; return i; }
static size_t cstart_in(const Snap *s, size_t i) { while (i > 0 && i < s->n && (s->tx[i] & 0xC0) == 0x80) i--; return i; }
static const char *const alpha[] = { "a", "b", " ", " ", "\n", "\n", "\xC3\xA9", "\xE2\x82\xAC", "\xE2\x88\x80", "\xF0\x9F\x98\x80", "\xF0\x9D\x94\xB8" };

static size_t randstr(unsigned char *b, unsigned max)
{
	size_t m = 0;
	for (unsigned k = rnd(max + 1); k > 0; k--) { const char *a = alpha[rnd(11)]; memcpy(b + m, a, strlen(a)); m += strlen(a); }
	return m;
}

/* the undo stack: the last command's edits, inverted */
static struct { size_t pos, ndel; unsigned char ins[64]; size_t nins; } inv[8]; static int ninv;

static void one_edit(void)
{
	unsigned char b[64]; size_t m = 0, pos = cstart(rnd((unsigned)n + 1)), ndel = 0;
	unsigned kind = rnd(10);
	if (kind < 4) m = randstr(b, 5);
	else if (kind < 7) ndel = cstart(pos + rnd(12)) - pos;
	else if (kind < 9) { ndel = cstart(pos + rnd(8)) - pos; m = randstr(b, 3); }
	else {                                                   /* a join */
		while (pos < n && tx[pos] != '\n') pos++;
		if (pos == n) return;
		ndel = 1;
		while (pos + ndel < n && tx[pos + ndel] == ' ') ndel++;
		b[0] = ' '; m = 1;
	}
	if (pos + ndel > n) ndel = n - pos;
	if (ndel > 60 || ned == MAXE || n - ndel + m >= N) return;
	if (ninv < 8) { inv[ninv].pos = pos; inv[ninv].ndel = m; memcpy(inv[ninv].ins, tx + pos, ndel); inv[ninv].nins = ndel; ninv++; }
	edit(pos, ndel, b, m);
}

static long newver(void)
{
	/* version = didChanges sent: a command with an edit bumps it */
	ver++;
	vs[ver] = snap(); vfirst[ver] = ned;
	return ver;
}

static int lastned;
static void command(int doflush)
{
	if (ninv && rnd(6) == 0) {                               /* undo the last command */
		for (int k = ninv - 1; k >= 0; k--) edit(inv[k].pos, inv[k].ndel, inv[k].ins, inv[k].nins);
		ninv = 0;
	} else {
		ninv = 0;
		for (unsigned k = 1 + (rnd(4) == 0 ? rnd(4) : 0); k > 0; k--) one_edit();
	}
	if (doflush && ned != lastned) { lsp_flush(d); newver(); lastned = ned; }
}

static void pos_of(const Snap *s, size_t b, long *l, long *c)
{
	*l = *c = 0;
	for (size_t i = 0; i < b; ) {
		unsigned char ch = s->tx[i];
		size_t len = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
		if (ch == '\n') ++*l, *c = 0; else *c += len == 4 ? 2 : 1;
		i += len;
	}
}

static char msgbuf[1 << 16];
static long lastpub = 0; static int serial; static int pending;   /* unflushed edits exist */

static void feed(const char *m, size_t len)
{
	char h[64]; int hn = snprintf(h, sizeof h, "Content-Length: %zu\r\n\r\n", len);
	static char all[1 << 17];
	memcpy(all, h, (size_t)hn); memcpy(all + hn, m, len);
	size_t tot = (size_t)hn + len, cut = rnd(3) ? tot : rnd((unsigned)tot + 1);
	if (cut) sfn(sctx, 1, all, cut);
	if (tot - cut) sfn(sctx, 1, all + cut, tot - cut);
}

static int cmpx(const void *a, const void *b)
{
	const LspDiag *x = a, *y = b;
	if (x->a != y->a) return x->a < y->a ? -1 : 1;
	if (x->z != y->z) return x->z < y->z ? -1 : 1;
	return strcmp(x->msg, y->msg);
}

/* does what is shown equal set x[0..m)? */
static int same(X *x, int m, int say)
{
	LspDiag exp[MAXD]; int ne = 0;
	for (int i = 0; i < m; i++) {
		if (x[i].dead) continue;
		long a = x[i].s < 0 ? (long)n : idx(id, n, x[i].s), z = x[i].point ? a : idx(id, n, x[i].e) + 1;
		exp[ne++] = (LspDiag){ (size_t)a, (size_t)z, 1, x[i].msg };
	}
	int ng; const LspDiag *g = lsp_diags(d, &ng);
	LspDiag got[MAXD * 2];
	if (ng > MAXD * 2) ng = MAXD * 2;
	if (ng) memcpy(got, g, (size_t)ng * sizeof *g);
	qsort(exp, (size_t)ne, sizeof *exp, cmpx); qsort(got, (size_t)ng, sizeof *got, cmpx);
	int ok = ne == ng;
	for (int i = 0; ok && i < ne; i++) ok = exp[i].a == got[i].a && exp[i].z == got[i].z && !strcmp(exp[i].msg, got[i].msg);
	if (!ok && say) {
		fprintf(stderr, "want %d:", ne); for (int i = 0; i < ne; i++) fprintf(stderr, " [%zu,%zu)%s", exp[i].a, exp[i].z, exp[i].msg);
		fprintf(stderr, "\ngot  %d:", ng); for (int i = 0; i < ng; i++) fprintf(stderr, " [%zu,%zu)%s", got[i].a, got[i].z, got[i].msg);
		fprintf(stderr, "\n");
	}
	return ok;
}

static long stat_pub, stat_lag, stat_drop, stat_ranges;

static int publish(void)
{
	long v = ver;
	if (rnd(2)) v = lastpub + (long)rnd((unsigned)(ver - lastpub) + 1);
	const Snap *s = &vs[v];
	int add = v == lastpub && lastpub > 0 && rnd(4) == 0;
	X nx[MAXD]; int nn = 0;
	char *m = msgbuf; 
	m += sprintf(m, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///x.c\",\"version\":%ld,%s\"diagnostics\":[", v, add ? "\"isIncremental\":true," : "");
	int cnt = (int)rnd(6);
	if (add && nwant + cnt > MAXD) cnt = 0;
	for (int i = 0; i < cnt; i++) {
		size_t a = cstart_in(s, rnd((unsigned)s->n + 1)), z = a;
		int point = rnd(4) == 0;
		if (!point) { z = cstart_in(s, a + 1 + rnd(rnd(3) ? 6 : 200)); if (z > s->n) z = s->n; if (z <= a) { point = 1; z = a; } }
		long l1, c1, l2, c2;
		pos_of(s, a, &l1, &c1); pos_of(s, z, &l2, &c2);
		X x = { a < s->n ? s->id[a] : -1, point ? 0 : s->id[z - 1], point, "", 0 };
		sprintf(x.msg, "#%d", ++serial);
		m += sprintf(m, "%s{\"range\":{\"start\":{\"line\":%ld,\"character\":%ld},\"end\":{\"line\":%ld,\"character\":%ld}},\"severity\":1,\"message\":\"%s\"}",
		             i ? "," : "", l1, c1, l2, c2, x.msg);
		for (int k = vfirst[v]; k < ned; k++) follow(&x, k);
		nx[nn++] = x;
	}
	m += sprintf(m, "]}}");
	int since = ned - vfirst[v];
	feed(msgbuf, (size_t)(m - msgbuf));
	stat_pub++; stat_ranges += cnt; if (v != ver || pending) stat_lag++;
	/* what should be shown now */
	X cand[MAXD]; int nc = 0;
	if (add) { memcpy(cand, want, (size_t)nwant * sizeof *cand); nc = nwant; }
	memcpy(cand + nc, nx, (size_t)nn * sizeof *cand); nc += nn;
	int okn = same(cand, nc, 0), oko = same(want, nwant, 0);
	if (okn && since <= 4096) { memcpy(want, cand, (size_t)nc * sizeof *cand); nwant = nc; lastpub = v; return 1; }
	if (oko && since > 2000) { stat_drop++; return 1; }      /* the log could not reach v: dropped, the old set stays */
	fprintf(stderr, "publication for v %ld (current %ld, %d edits since, add %d, pending %d): wrong\n", v, ver, since, add, pending);
	same(cand, nc, 1);
	return 0;
}

int main(int argc, char **argv)
{
	unsigned long long seed = argc > 1 ? strtoull(argv[1], 0, 10) : 1;
	int trials = argc > 2 ? atoi(argv[2]) : 100, burst = argc > 3;
	static const char *const av[] = { "x", NULL };
	static const LspServer tab[] = { { ".c", "c", av, av, 0 }, { 0 } };
	lsp_servers = tab;
	for (int tr = 0; tr < trials; tr++) {
		rs = seed * 1000003ULL + (unsigned long long)tr;
		n = 0; ned = 0; ver = 0; nwant = 0; lastpub = 0; lastned = 0; ninv = 0; pending = 0;
		unsigned char b[400];
		for (int k = 0; k < 12; k++) { size_t m = randstr(b, 20); memcpy(tx + n, b, m); for (size_t j = 0; j < m; j++) id[n + j] = nextid++; n += m; }
		t = text_new((const char *)tx, n);
		d = lsp_open("/x.c", t);
		if (!d) { fprintf(stderr, "no doc\n"); return 2; }
		const char *init = "{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{}}";
		feed(init, strlen(init));
		vs[0] = snap(); vfirst[0] = 0;
		int steps = burst ? 12000 : 400;
		for (int st = 0; st < steps && ned < MAXE - 16 && ver < MAXV - 2; st++) {
			unsigned r = rnd(burst ? 3000 : 10);
			if (r < (burst ? 2998u : 6u)) { int fl = rnd(12) != 0; command(fl); pending = lastned != ned; }
			else if (!publish()) { fprintf(stderr, "seed %llu trial %d step %d\n", seed, tr, st); return 1; }
			/* shown ranges follow every edit */
			if (!same(want, nwant, 0)) {
				fprintf(stderr, "after an edit: wrong (seed %llu trial %d step %d)\n", seed, tr, st); same(want, nwant, 1); return 1;
			}
		}
		lsp_close(d); text_free(t);
	}
	printf("seed %llu: %d trials, %ld publications (%ld for a left version or with edits pending, %ld dropped by a full log), %ld ranges: all as the oracle\n",
	       seed, trials, stat_pub, stat_lag, stat_drop, stat_ranges);
	return 0;
}
