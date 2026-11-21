/* Package 13: the LSP client against a fake server that delays, reorders
   and drops (Lean cancels stale elaboration), publishes for versions the
   client has left, sends ranges and incremental batches, asks the editor
   things, and dies.  The checks are LspSync.tla's properties as tests:
   - Correct: whatever is shown points at the characters the server meant.
     Every server message names what it meant (its "ghost": the bytes of its
     range in the version it is for, the character at a reply's position).
     Each byte carries an id here, every version's text and ids are kept,
     and every edit is recorded, so the test follows a range's ends from
     the version it was meant for through each edit since, as extmarks,
     and the shown range must sit exactly there;
   - CatchUp: once editing stops, the diagnostics for the current version
     are shown within 2 s;
   and the server checks the protocol: versions +1, nothing before the
   initialize reply, every change applies, a new request cancels the last,
   a reply for each of its requests.  The test binary is also the fake
   server (run with --fake). */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "text.h"
#include "../src/lsp/json.h"

static long long ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* --- an independent UTF-16 reference: whole code points, valid UTF-8 ----- */

static size_t cplen(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

/* A model of lsp.h's u16len, apart from the kernel: the bytes of the
   character at i, a lead byte and the continuations its value asks for,
   else 1. */
static size_t mstep(const unsigned char *s, size_t n, size_t i)
{
	unsigned c = s[i];
	size_t want = c < 0x80 ? 1 : c >= 0xC0 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF8 ? 4 : 0;
	if (want < 2 || i + want > n) return 1;
	for (size_t j = 1; j < want; j++) if ((s[i + j] & 0xC0) != 0x80) return 1;
	return want;
}

/* Whether those k bytes are UTF-8, by the code point they encode. */
static int mutf8(const unsigned char *s, size_t k)
{
	if (k == 1) return s[0] < 0x80;
	unsigned long cp = s[0] & (0x7F >> k);
	for (size_t j = 1; j < k; j++) cp = cp * 64 + (s[j] & 0x3F);
	unsigned long least = k == 2 ? 0x80 : k == 3 ? 0x800 : 0x10000;
	return cp >= least && cp <= 0x10FFFF && !(cp >= 0xD800 && cp < 0xE000);
}

/* s as the server must get it: what is not UTF-8 as U+FFFD, one per
   UTF-16 unit, and for each byte sent the id of the byte it stands for
   (U+FFFD's first byte the character's first, its others the last). */
static const char FFFD[] = "\xEF\xBF\xBD";
static size_t msent(const unsigned char *s, size_t n, const int *sid, char *out, int *oid)
{
	size_t o = 0;
	for (size_t i = 0, k; i < n; i += k) {
		k = mstep(s, n, i);
		if (mutf8(s + i, k)) {
			for (size_t j = 0; j < k; j++) { out[o] = (char)s[i + j]; oid[o++] = sid[i + j]; }
			continue;
		}
		for (int u = 0; u < (k == 4 ? 2 : 1); u++)
			for (int j = 0; j < 3; j++) { out[o] = FFFD[j]; oid[o++] = sid[j ? i + k - 1 : i]; }
	}
	out[o] = 0;
	return o;
}

/* ======================================================================== */
/* The fake server.                                                          */

static FILE *viol;                    /* where protocol violations go */
static char sdoc[1 << 16];            /* the server's copy of the text */
static size_t sn;
static long sver = -1, serial;
static int opened, inited;
static char suri[4096];
static Jtok *ft;
static int fcap;

static void wrong(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(viol, "WRONG (server %d): ", (int)getpid());
	vfprintf(viol, fmt, ap);
	fputc('\n', viol);
	fflush(viol);
	va_end(ap);
	exit(3);
}

static void fsend(Str *m)
{
	char h[40];
	int n = snprintf(h, sizeof h, "Content-Length: %zu\r\n\r\n", m->n);
	if (write(1, h, (size_t)n) < 0 || write(1, m->s, m->n) < 0) exit(0);
}

/* (line, UTF-16 column) <-> byte, in the server's copy, code point by code point */
static size_t fbyte(long l, long c)
{
	size_t i = 0;
	for (; l > 0; l--) {
		while (i < sn && sdoc[i] != '\n') i++;
		if (i == sn) wrong("line past the end");
		i++;
	}
	for (; c > 0; ) {
		if (i == sn || sdoc[i] == '\n') wrong("column past the line");
		unsigned char b = (unsigned char)sdoc[i];
		c -= cplen(b) == 4 ? 2 : 1;
		i += cplen(b);
		if (c < 0) wrong("column inside a character");
	}
	return i;
}

/* The text a publication is about: the current one or an older version's. */
static const char *pd;
static size_t pn;
static long pv;
static struct { long v; char *text; size_t n; } old[16];   /* the last versions, by v % 16 */

static void fpos(Str *m, size_t b)
{
	long l = 0, c = 0;
	for (size_t i = 0; i < b; i += cplen((unsigned char)pd[i]))
		if (pd[i] == '\n') l++, c = 0;
		else c += cplen((unsigned char)pd[i]) == 4 ? 2 : 1;
	str_addf(m, "{\"line\":%ld,\"character\":%ld}", l, c);
}

static size_t fchar(size_t i) { while (i > 0 && i < pn && (pd[i] & 0xC0) == 0x80) i--; return i; }   /* a character start */

/* A diagnostic: a random range, its message "#pid.serial v n a z ghost":
   n is how many diagnostics version v has once this message is in, a and z
   the bytes of the range in version v. */
static int quiet;                     /* after a marker: nothing more is said until a request is answered (1) and then a '!' typed (2) */

static void fdiag(Str *m, int point, long n)
{
	size_t a = fchar(rnd((unsigned)pn + 1)), z = point ? a : fchar(a + rnd((unsigned)(pn - a) + 1));
	if (!point && (quiet || rnd(8) == 0)) a = 0, z = pn;
	if (z < a) z = a;
	str_lit(m, "{\"range\":{\"start\":");
	fpos(m, a);
	str_lit(m, ",\"end\":");
	fpos(m, z);
	str_addf(m, "},\"severity\":%u,\"message\":\"#%d.%ld %ld %ld %zu %zu ", 1 + rnd(4), (int)getpid(), ++serial, pv, n, a, z);
	str_addq(m, pd + a, z - a);
	str_lit(m, "\"}");
}

static int incr;                      /* a full set went out for sver: incremental may follow */
static long ndiag;                    /* diagnostics sent for sver */

static long lastpub = -1;             /* the newest version published */

static void fpublish(void)
{
	Str m = { 0 };
	/* mostly the current version; sometimes one the editor has left, in
	   order (Lean elaborating behind the typing), and now and then one
	   older than the last published (a reporter late after a reopen) */
	unsigned r = rnd(10);
	long v = sver;
	if (r < 3) {
		long lo = r < 2 ? (lastpub > sver - 15 ? lastpub : sver - 15) : sver - 15, hi = r < 2 ? sver : lastpub - 1;
		if (lo < 0) lo = 0;
		if (hi >= lo) {
			long w = lo + (long)rnd((unsigned)(hi - lo + 1));
			if (old[w % 16].v == w && old[w % 16].text) v = w;
		}
	}
	if (v == sver) { pd = sdoc; pn = sn; }
	else { pd = old[v % 16].text; pn = old[v % 16].n; incr = 0; }
	pv = v;
	if (v > lastpub) lastpub = v;
	int inc = v == sver && incr && rnd(2);
	str_addf(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"version\":%ld,", suri, v);
	if (inc) str_lit(&m, "\"isIncremental\":true,");
	str_lit(&m, "\"diagnostics\":[");
	unsigned n = 1 + rnd(3);
	ndiag = inc ? ndiag + n : n;
	for (unsigned k = 0; k < n; k++) { if (k) str_lit(&m, ","); fdiag(&m, rnd(6) == 0, ndiag); }
	str_lit(&m, "]}}");
	fsend(&m);
	free(m.s);
	incr = v == sver;
	pd = sdoc; pn = sn; pv = sver;
	if (rnd(3) == 0) {
		m = (Str){ 0 };
		str_addf(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"$/lean/fileProgress\",\"params\":{\"textDocument\":{\"uri\":\"%s\",\"version\":%ld},\"processing\":[", suri, sver);
		lastpub = sver;
		if (rnd(2)) fdiag(&m, 0, 0);
		str_lit(&m, "]}}");
		fsend(&m);
		free(m.s);
	}
}

/* A request parked to be answered later, after more changes maybe. */
static struct { long id; long l, c; int cancelled; } park[64];
static int npark;
static long sreq;                     /* our requests to the editor not yet answered */
static long long sreq_at;             /* when the last one went */
static int owed;                      /* a request went before the marker's answer: the editor's reply comes before its '!' */

static void freq(void)
{
	Str m = { 0 };
	str_addf(&m, "{\"jsonrpc\":\"2.0\",\"id\":\"s%ld\",\"method\":\"workspace/configuration\",\"params\":{\"items\":[{}]}}", ++serial);
	fsend(&m);
	free(m.s);
	sreq++;
	sreq_at = ms();
}

static void fanswer(int k)
{
	Str m = { 0 };
	if (rnd(5) == 0) {
		str_addf(&m, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"error\":{\"code\":-32800,\"message\":\"cancelled\"}}", park[k].id);
	} else {
		/* the position on the server's text now, which may be newer than
		   the request's (LspSync's "mapreply" mistake) */
		size_t lines = 0, b = 0;
		for (size_t i = 0; i < sn; i++) lines += sdoc[i] == '\n';
		if ((size_t)park[k].l > lines) b = sn;
		else {
			for (long l = park[k].l; l > 0; l--) { while (sdoc[b] != '\n') b++; b++; }
			for (long c = park[k].c; c > 0 && b < sn && sdoc[b] != '\n'; b += cplen((unsigned char)sdoc[b]))
				c -= cplen((unsigned char)sdoc[b]) == 4 ? 2 : 1;
		}
		size_t z = b < sn ? b + cplen((unsigned char)sdoc[b]) : b;
		str_addf(&m, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":", park[k].id);
		if (quiet == 1) owed = sreq > 0, quiet = 2;   /* still silent: the test looks before it types '!' */
		/* a hover's MarkupContent or MarkedString[] */
		static const char *const form[][2] = { { "{\"contents\":{\"kind\":\"markdown\",\"value\":\"", "\"}}}" },
			{ "{\"contents\":[{\"language\":\"lean\",\"value\":\"", "\"}]}}" } };
		int f = (int)rnd(2);
		str_addf(&m, "%s", form[f][0]);
		str_addf(&m, "#%d.%ld %ld 0 0 0 ", (int)getpid(), ++serial, sver);
		str_addq(&m, sdoc + b, z - b);
		str_addf(&m, "%s", form[f][1]);
	}
	fsend(&m);
	free(m.s);
	park[k] = park[--npark];
}

static void fapply(const char *m, int ch)
{
	for (int k = 0, e; (e = json_at(ft, ch, k)) >= 0; k++) {
		int r = json_get(m, ft, e, "range"), s = json_get(m, ft, r, "start"), z = json_get(m, ft, r, "end"), t = json_get(m, ft, e, "text");
		if (r < 0) {                                  /* the whole text */
			if (t < 0 || ft[t].z - ft[t].a >= sizeof sdoc) wrong("bad whole text");
			sn = json_str(m, ft, t, sdoc);
			continue;
		}
		size_t a = fbyte(json_int(m, ft, json_get(m, ft, s, "line")), json_int(m, ft, json_get(m, ft, s, "character")));
		size_t b = fbyte(json_int(m, ft, json_get(m, ft, z, "line")), json_int(m, ft, json_get(m, ft, z, "character")));
		if (b < a || t < 0) wrong("bad range");
		char ins[4096];
		size_t n = json_str(m, ft, t, ins);
		if (sn - (b - a) + n > sizeof sdoc) wrong("too long");
		memmove(sdoc + a + n, sdoc + b, sn - b);
		memcpy(sdoc + a, ins, n);
		sn = sn - (b - a) + n;
	}
}

static void fkeep(void)
{
	int k = (int)(sver % 16);
	free(old[k].text);
	old[k].text = malloc(sn + 1);
	if (!old[k].text) wrong("out of memory");
	memcpy(old[k].text, sdoc, sn);
	old[k].n = sn;
	old[k].v = sver;
}

static size_t fbehind;   /* bytes read behind the message being handled */

/* The goals window's server (seed "goals"): Lean's RPC with one goal per
   non-empty line (two where it holds λ), an expected type T<character> on odd lines,
   and for each version a warning at each '!' (its fullRange to the next
   line) and goals accomplished (a silent diagnostic) on lines starting
   "done", and a message wholly past the end of a line holding '?'.  It says
   nothing else unasked. */
static int gmode;
#define GROOT "/tmp/homeros goals \xC3\xBC"
#define GROOT_URI "/tmp/homeros%20goals%20%C3%BC"
static long fsess;                    /* the RPC session; an older one is outdated */
static struct { long id, sess, l, c; int term; } rpc;   /* the question out about goals, id 0 none */

static void fline(long l, const char **b, size_t *n)
{
	const char *p = sdoc, *e = sdoc + sn;
	for (; l > 0 && p < e; l--) { const char *q = memchr(p, '\n', (size_t)(e - p)); p = q ? q + 1 : e; }
	const char *q = memchr(p, '\n', (size_t)(e - p));
	*b = p; *n = (size_t)((q ? q : e) - p);
}

static void fsay(void)
{
	Str m = { 0 };
	/* each version's first publication incremental, as Lean's can be */
	str_addf(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"isIncremental\":true,\"version\":%ld,", suri, sver);
	str_lit(&m, "\"diagnostics\":[");
	int k = 0;
	for (long l = 0;; l++) {
		const char *b;
		size_t n;
		fline(l, &b, &n);
		if (b == sdoc + sn) break;
		const char *x = memchr(b, '!', n);
		if (x) {
			size_t c = lsp_u16((const unsigned char *)b, (size_t)(x - b));
			str_addf(&m, "%s{\"range\":{\"start\":{\"line\":%ld,\"character\":%zu},\"end\":{\"line\":%ld,\"character\":%zu}},"
			         "\"fullRange\":{\"start\":{\"line\":%ld,\"character\":%zu},\"end\":{\"line\":%ld,\"character\":0}},\"severity\":2,\"message\":\"bang\"}",
			         k++ ? "," : "", l, c, l, c + 1, l, c, l + 1);
		}
		if (memchr(b, '?', n)) {               /* wholly past the line's end: LSP says at its end */
			size_t c = lsp_u16((const unsigned char *)b, n);
			str_addf(&m, "%s{\"range\":{\"start\":{\"line\":%ld,\"character\":%zu},\"end\":{\"line\":%ld,\"character\":%zu}},"
			         "\"severity\":3,\"message\":\"far\"}", k++ ? "," : "", l, c + 2, l, c + 5);
		}
		if (n >= 4 && !memcmp(b, "done", 4))
			str_addf(&m, "%s{\"range\":{\"start\":{\"line\":%ld,\"character\":0},\"end\":{\"line\":%ld,\"character\":4}},"
			         "\"severity\":3,\"message\":\"Goals accomplished!\",\"isSilent\":true}", k++ ? "," : "", l, l);
	}
	str_lit(&m, "]}}");
	fsend(&m);
	m.n = 0;
	str_addf(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"$/lean/fileProgress\",\"params\":{\"textDocument\":{\"uri\":\"%s\",\"version\":%ld},\"processing\":[]}}", suri, sver);
	fsend(&m);
	free(m.s);
}

static void frpc(void)
{
	Str m = { 0 };
	str_addf(&m, "{\"jsonrpc\":\"2.0\",\"id\":%ld,", rpc.id);
	const char *b;
	size_t n;
	fline(rpc.l, &b, &n);
	if (rpc.sess != fsess || rnd(6) == 0) {             /* Lean elaborated anew */
		fsess++;
		str_lit(&m, "\"error\":{\"code\":-32900,\"message\":\"Outdated RPC session\"}}");
	} else if (rpc.term) {
		if (rpc.l % 2) str_addf(&m, "\"result\":{\"range\":{\"start\":{\"line\":%ld,\"character\":0},\"end\":{\"line\":%ld,\"character\":1}},"
		                       "\"hyps\":[],\"type\":{\"text\":\"T%ld\"}}}", rpc.l, rpc.l, rpc.c);
		else str_lit(&m, "\"result\":null}");
	} else {
		str_lit(&m, "\"result\":{\"goals\":[");
		for (int k = 0; n && k < 1 + (memmem(b, n, "\xCE\xBB", 2) != NULL); k++) {
			str_addz(&m, k ? ",{" : "{");
			str_lit(&m, "\"userName\":\"c\",\"hyps\":[{\"names\":[\"h\",\"\xE2\x9C\x9Di\"],\"isInserted\":true,\"type\":{\"text\":\"x\"}}],"
			        "\"goalPrefix\":\"\xE2\x8A\xA2 \",\"type\":{\"tag\":[{\"diffStatus\":\"wasChanged\"},{\"append\":[{\"text\":\"");
			str_addq(&m, b, n);
			str_lit(&m, "\"}]}]}}");
		}
		str_lit(&m, "]}}");
	}
	fsend(&m);
	free(m.s);
	rpc.id = 0;
}

static void fhandle(const char *m, size_t n)
{
	for (size_t i = 0, k; i < n; i += k)                  /* clangd and Lean refuse it */
		if (!mutf8((const unsigned char *)m + i, k = mstep((const unsigned char *)m, n, i))) wrong("a message that is not UTF-8");
	if (json_parse(m, n, &ft, &fcap) < 0) wrong("not JSON: %.*s", (int)n, m);
	int id = json_get(m, ft, 0, "id"), me = json_get(m, ft, 0, "method"), p = json_get(m, ft, 0, "params");
	char meth[64] = "";
	if (me >= 0 && ft[me].z - ft[me].a < sizeof meth) json_str(m, ft, me, meth);
	if (me < 0) {                                         /* a reply to us */
		if (id < 0 || json_get(m, ft, 0, "result") < 0) wrong("bad reply");
		sreq--;
		return;
	}
	if (!strcmp(meth, "initialize")) {
		if (inited) wrong("initialize twice");
		if (json_get(m, ft, json_get(m, ft, json_get(m, ft, p, "capabilities"), "lean"), "incrementalDiagnosticSupport") < 0)
			wrong("no incremental diagnostics capability");
		if (json_get(m, ft, json_get(m, ft, json_get(m, ft, p, "capabilities"), "lean"), "silentDiagnosticSupport") < 0)
			wrong("no silent diagnostics capability: goals accomplished would show as a message");
		/* the goals test's file is in a subdirectory of its project, whose
		   name needs percent-encoding */
		char ru[256] = "", cwd[256];
		int x = json_get(m, ft, p, "rootUri");
		if (x >= 0 && ft[x].z - ft[x].a < sizeof ru) json_str(m, ft, x, ru);
		if (gmode && (strcmp(ru, "file://" GROOT_URI) || !getcwd(cwd, sizeof cwd) || strcmp(cwd, GROOT)))
			wrong("the project is %s, not %s", ru, GROOT_URI);
		if (rnd(2)) usleep(rnd(30000));               /* messages pile up behind it */
		int behind = 0;            /* bytes waiting (not an end: the editor may close a server it just started) */
		if (fbehind || (ioctl(0, FIONREAD, &behind) == 0 && behind > 0)) wrong("a message sent before the initialize reply");
		Str r = { 0 };
		str_addf(&r, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"capabilities\":{\"textDocumentSync\":2}}}", json_int(m, ft, id));
		fsend(&r);
		free(r.s);
		inited = 1;
		return;
	}
	if (!inited) wrong("%s before the initialize reply", meth);
	/* the test saves to let the server die: the test then knows when */
	if (!strcmp(meth, "textDocument/didSave") && rnd(2)) exit(0);
	if (!strcmp(meth, "initialized") || !strcmp(meth, "$/cancelRequest") || !strcmp(meth, "textDocument/didSave")) {
		if (meth[0] == '$') for (int k = 0; k < npark; k++)
			if (park[k].id == json_int(m, ft, json_get(m, ft, p, "id"))) {
				park[k].cancelled = 1;
				if (rnd(2)) fanswer(k);
				break;
			}
		return;
	}
	int td = json_get(m, ft, p, "textDocument");
	if (!strcmp(meth, "textDocument/didOpen")) {
		if (opened) wrong("didOpen twice");
		int u = json_get(m, ft, td, "uri"), t = json_get(m, ft, td, "text");
		json_str(m, ft, u, suri);
		if (ft[t].z - ft[t].a >= sizeof sdoc) wrong("too long");
		sn = json_str(m, ft, t, sdoc);
		sver = json_int(m, ft, json_get(m, ft, td, "version"));
		opened = 1; incr = 0;
		fkeep();
		if (gmode) fsay();
	} else if (!strcmp(meth, "textDocument/didClose")) {
		if (!opened) wrong("didClose before didOpen");
		opened = 0;
	} else if (!strcmp(meth, "textDocument/didChange")) {
		if (!opened) wrong("didChange before didOpen");
		long v = json_int(m, ft, json_get(m, ft, td, "version"));
		if (v != sver + 1) wrong("version %ld after %ld", v, sver);
		if (quiet == 2 && memchr(m, '!', n)) {
			if (owed && sreq) wrong("the editor never answered our request");
			owed = quiet = 0;
		}
		fapply(m, json_get(m, ft, p, "contentChanges"));
		sver = v; incr = 0;
		fkeep();
		if (gmode) fsay();
		/* a typed '@' (the test's marker): one diagnostic over the whole
		   text, at once, then silence until a request is answered, so the
		   test knows it is the last thing said before the answer */
		if (memchr(m, '@', n)) {
			Str d = { 0 };
			pd = sdoc; pn = sn; pv = sver;
			quiet = 1;
			str_addf(&d, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"version\":%ld,\"diagnostics\":[", suri, sver);
			fdiag(&d, 0, ndiag = 1);
			str_lit(&d, "]}}");
			fsend(&d);
			free(d.s);
			if (!sreq) freq();       /* and asks something, which the editor answers before it reads the answer */
		}
	} else if (!strcmp(meth, "textDocument/hover")) {
		int q = json_get(m, ft, p, "position");
		if (npark == 64) wrong("64 requests parked");
		park[npark].id = json_int(m, ft, id);
		park[npark].cancelled = 0;
		/* the design: a new request cancels the last of its kind */
		for (int k = 0; k < npark; k++)
			if (!park[k].cancelled) wrong("%s without cancelling %ld", meth, park[k].id);
		park[npark].l = json_int(m, ft, json_get(m, ft, q, "line"));
		park[npark].c = json_int(m, ft, json_get(m, ft, q, "character"));
		fbyte(park[npark].l, park[npark].c);       /* must be a position in the text */
		npark++;
	} else if (!strcmp(meth, "shutdown")) {
		if (sreq && ms() - sreq_at > 300) wrong("the editor never answered our request");
		Str r = { 0 };
		str_addf(&r, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", json_int(m, ft, id));
		fsend(&r);
		free(r.s);
	} else if (!strcmp(meth, "exit")) {
		exit(0);
	} else if (!strcmp(meth, "$/test/echo")) {          /* the test's raw request: the version it saw */
		Str r = { 0 };
		str_addf(&r, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":%ld}", json_int(m, ft, id), sver);
		fsend(&r);
		free(r.s);
	} else if (!strcmp(meth, "$/test/note")) {          /* its raw notification, heard back on lsp_tap */
		Str r = { 0 };
		str_lit(&r, "{\"jsonrpc\":\"2.0\",\"method\":\"$/test/noted\",\"params\":{}}");
		fsend(&r);
		free(r.s);
	} else if (!strcmp(meth, "$/test/work")) {          /* two tokens begun, one reported on, one ended: 1 at work */
		static const char *const kind[] = { "begin", "begin", "report", "end" };
		for (int i = 0; i < 4; i++) {
			Str r = { 0 };
			str_addf(&r, "{\"jsonrpc\":\"2.0\",\"method\":\"$/progress\",\"params\":{\"token\":%d,\"value\":{\"kind\":\"%s\"}}}", i == 1 ? 2 : 1, kind[i]);
			fsend(&r);
			free(r.s);
		}
	} else if (!strcmp(meth, "$/lean/rpc/connect")) {
		Str r = { 0 };
		str_addf(&r, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"sessionId\":\"%ld\"}}", json_int(m, ft, id), ++fsess);
		fsend(&r);
		free(r.s);
	} else if (!strcmp(meth, "$/lean/rpc/call")) {
		if (rpc.id) wrong("a question about goals before the last was answered");
		char sid[32] = "", mm[64] = "";
		int x = json_get(m, ft, p, "sessionId"), y = json_get(m, ft, p, "method");
		if (x >= 0 && ft[x].z - ft[x].a < sizeof sid) json_str(m, ft, x, sid);
		if (y >= 0 && ft[y].z - ft[y].a < sizeof mm) json_str(m, ft, y, mm);
		int q = json_get(m, ft, json_get(m, ft, p, "params"), "position");
		rpc.id = json_int(m, ft, id);
		rpc.sess = atol(sid);
		rpc.term = !strcmp(mm, "Lean.Widget.getInteractiveTermGoal");
		if (!rpc.term && strcmp(mm, "Lean.Widget.getInteractiveGoals")) wrong("rpc %s", mm);
		rpc.l = json_int(m, ft, json_get(m, ft, q, "line"));
		rpc.c = json_int(m, ft, json_get(m, ft, q, "character"));
		fbyte(rpc.l, rpc.c);   /* a position in the text */
	} else if (!strcmp(meth, "$/test/die")) {
		exit(0);
	} else if (!strcmp(meth, "$/test/plain")) {      /* no diagnostics, no version: Neovim takes the latest */
		Str d = { 0 };
		str_addf(&d, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"diagnostics\":[]}}", suri);
		fsend(&d);
		free(d.s);
	} else wrong("unknown %s", meth);
}

static int fake(const char *seed, const char *log)
{
	static char in[1 << 21];
	size_t n = 0;
	rng ^= strtoull(seed, NULL, 10) * 0x9E3779B97F4A7C15ULL;
	gmode = !strcmp(seed, "goals");
	viol = fopen(log, "a");
	if (!viol) return 2;
	for (;;) {
		struct pollfd pf = { 0, POLLIN, 0 };
		if (poll(&pf, 1, (int)rnd(15)) > 0) {
			ssize_t r = read(0, in + n, sizeof in - n);
			if (r <= 0) return 0;
			n += (size_t)r;
			for (;;) {
				char *e = memmem(in, n, "\r\n\r\n", 4);
				if (!e) break;
				size_t h = (size_t)(e - in) + 4, len = strtoul(in + 15, NULL, 10);
				if (strncmp(in, "Content-Length: ", 16)) wrong("bad header");
				if (n - h < len) break;
				fbehind = n - h - len;
				fhandle(in + h, len);
				memmove(in, in + h + len, n - h - len);
				n -= h + len;
			}
			continue;
		}
		if (!inited || !opened) continue;
		if (gmode) { if (rpc.id && rnd(3) == 0) frpc(); continue; }
		/* idle: do something the editor must cope with */
		unsigned a = rnd(100);
		if (a < 40 && !quiet) fpublish();
		else if (a < 70 && npark) fanswer((int)rnd((unsigned)npark));
		else if (a < 72 && !sreq && !quiet) freq();
	}
}

/* ======================================================================== */
/* The editor side.                                                          */

static Text *t;
static int ids[1 << 16];             /* one per byte of t */
static size_t len;
static int nextid = 1;
static char buf[1 << 16];

/* What the test knows of each diagnostic it has seen, by serial: its ends
   as extmarks, s the id of its first character (0: the end of the text),
   e of its last (-1: before the start); a point has only s. */
typedef struct { char key[32]; int s, e, point; } Seen;
static Seen seen[4096];
static int nseen;
static long stat_new, stat_old, stat_moved, stat_answers;   /* what the checks saw */

/* Every version's text and ids, and every edit with the ids it removed
   and its neighbours, so a diagnostic for an older version can be
   followed to now. */
typedef struct { char *text; int *id; size_t n; } Snap;
typedef struct { long ver; int *gone; size_t n; int after, before; } Hist;
static Snap snap[2048];
static Hist hist[1 << 14];
static long cur;                      /* the version the client is at */
static int nhist;

static void snapshot(void)
{
	CHECK(cur < 2048);
	snap[cur].text = malloc(3 * len + 1);
	snap[cur].id = malloc((3 * len + 1) * sizeof (int));
	CHECK(snap[cur].text && snap[cur].id);
	text_read(t, 0, buf, len);
	snap[cur].n = msent((const unsigned char *)buf, len, ids, snap[cur].text, snap[cur].id);
}

/* a deleted first character hands the start to the one after the
   deletion, a deleted last character the end to the one before it */
static void follow(const Hist *h, int *s, int *e, int point)
{
	for (size_t i = 0; i < h->n; i++) {
		if (*s == h->gone[i]) *s = h->after;
		if (!point && *e == h->gone[i]) *e = h->before;
	}
}

static void edit(LspDoc *d, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	CHECK(nhist < 1 << 14);
	Hist *h = &hist[nhist++];
	h->ver = cur;
	h->n = ndel;
	h->gone = malloc((ndel + 1) * sizeof (int));
	CHECK(h->gone);
	memcpy(h->gone, ids + pos, ndel * sizeof (int));
	h->after = pos + ndel < len ? ids[pos + ndel] : 0;
	h->before = pos ? ids[pos - 1] : -1;
	for (int k = 0; k < nseen; k++) follow(h, &seen[k].s, &seen[k].e, seen[k].point);
	lsp_edit(d, t, pos, ndel, ins, nins);
	CHECK(!text_delete(t, pos, ndel));
	CHECK(!text_insert(t, pos, ins, nins));
	memmove(ids + pos + nins, ids + pos + ndel, (len - pos - ndel) * sizeof *ids);
	for (size_t i = 0; i < nins; i++) ids[pos + i] = nextid++;
	len = len - ndel + nins;
	CHECK(text_len(t) == len);
}

/* the start of the character holding byte i, as the model counts them */
static size_t charstart(size_t i)
{
	text_read(t, 0, buf, len);
	size_t j = 0;
	while (j < len && j + mstep((const unsigned char *)buf, len, j) <= i) j += mstep((const unsigned char *)buf, len, j);
	return j;
}

static long where(int id) { for (size_t i = 0; i < len; i++) if (ids[i] == id) return (long)i; return -1; }

/* Correct: each shown diagnostic points at what the server meant. */
static void check_shown(LspDoc *d, long *latest)
{
	int n, np;
	const LspDiag *g = lsp_diags(d, &n), *p = lsp_progress(d, &np);
	text_read(t, 0, buf, len);
	int fresh = 0, now = 0;
	long most = 0;
	for (int i = 0; i < np; i++) CHECK(p[i].a <= p[i].z && p[i].z <= len);
	for (int i = 0; i < n; i++) {
		CHECK(g[i].a <= g[i].z && g[i].z <= len);
		CHECK(i == 0 || g[i - 1].a <= g[i].a);
		CHECK(g[i].msg && g[i].msg[0] == '#' && g[i].sev >= 1 && g[i].sev <= 4);
		char key[32];
		long v, cnt;
		size_t a, z;
		int k = 0, off;
		CHECK(sscanf(g[i].msg, "%31s %ld %ld %zu %zu%n", key, &v, &cnt, &a, &z, &off) == 5);
		const char *ghost = g[i].msg + off + 1;
		if (v > *latest) *latest = v;
		if (v == cur) { now++; if (cnt > most) most = cnt; }
		while (k < nseen && strcmp(seen[k].key, key)) k++;
		if (k == nseen) {
			/* newly shown: the server meant bytes [a, z) of version v; the
			   test follows them through the edits made since */
			CHECK(v >= 0 && v <= cur && snap[v].text);
			CHECK(a <= z && z <= snap[v].n && z - a == strlen(ghost) && !memcmp(snap[v].text + a, ghost, z - a));
			CHECK(nseen < 4096);
			Seen *e = &seen[nseen++];
			strcpy(e->key, key);
			e->point = a == z;
			e->s = a < snap[v].n ? snap[v].id[a] : 0;
			e->e = z > a ? snap[v].id[z - 1] : -1;
			for (int h = 0; h < nhist; h++) if (hist[h].ver >= v) follow(&hist[h], &e->s, &e->e, e->point);
			if (v == cur) { stat_new++; fresh = 1; } else stat_old++;
		} else stat_moved++;
		/* where its marks are now */
		long fa = seen[k].s ? where(seen[k].s) : (long)len, fz = seen[k].e > 0 ? where(seen[k].e) : -1;
		CHECK(fa >= 0);
		if (seen[k].point) { CHECK((size_t)fa == g[i].a && g[i].z == g[i].a); continue; }
		CHECK(fz >= fa);       /* else nothing is left of it, and it must be gone */
		CHECK((size_t)fa == g[i].a && (size_t)fz + 1 == g[i].z);
	}
	/* all of the current version's diagnostics, incremental ones added to
	   the full set (none can have been dropped by an edit: an edit makes
	   a new version) */
	if (fresh) CHECK(now == most);
}

static const char *const alpha[] = { "a", "b", " ", "\n", "é", "€", "𝓝", "∀", "\n\n", "xy" };
/* what is not UTF-8: a Latin-1 byte, a lone continuation, an overlong
   form, a surrogate, a cut-off character (in every third scenario) */
static const char *const junk[] = { "\xE9", "\x80", "\xC0\x80", "\xED\xA0\x80", "\xE2\x82" };

/* lsp_tap (package 14's infoview): out 1 only the edits and closes, out 0
   only what the server says unasked and its answer to initialize */
static long tapver = -1, nnoted;
static char tapinit[4096];
static size_t ntapinit;

static void tap(int out, const char *m, size_t n)
{
	Jtok *k = NULL;
	int cap = 0;
	CHECK(json_parse(m, n, &k, &cap) > 0);
	int me = json_get(m, k, 0, "method"), id = json_get(m, k, 0, "id");
	if (out) {
		CHECK(me >= 0 && id < 0);
		if (!strncmp(m + k[me].a, "textDocument/didChange\"", 23))
			tapver = json_int(m, k, json_get(m, k, json_get(m, k, json_get(m, k, 0, "params"), "textDocument"), "version"));
		else CHECK(!strncmp(m + k[me].a, "textDocument/didClose\"", 22));
	} else if (me >= 0) {
		if (!strncmp(m + k[me].a, "$/test/noted\"", 13)) nnoted++;
	} else {
		CHECK(json_int(m, k, id) == 0 && json_get(m, k, json_get(m, k, 0, "result"), "capabilities") >= 0 && n < sizeof tapinit);
		memcpy(tapinit, m, n);
		ntapinit = n;
	}
	free(k);
}

/* lsp_send's answers: how many, the last one's result, or NULL (the server died) */
typedef struct { int calls, null; long ver; } Got;

static void got(void *ctx, const char *m, size_t n)
{
	Got *g = ctx;
	g->calls++;
	g->null = !m;
	if (!m) return;
	Jtok *k = NULL;
	int cap = 0;
	CHECK(json_parse(m, n, &k, &cap) > 0);
	g->ver = json_int(m, k, json_get(m, k, 0, "result"));
	free(k);
}

static void scenario(int seed, const char *log)
{
	static char exe[4096], sseed[24], *const roots[] = { NULL };
	static const char *argv[5];
	static LspServer fake_table[2];
	snprintf(sseed, sizeof sseed, "%d", seed);
	argv[0] = exe; argv[1] = "--fake"; argv[2] = sseed; argv[3] = log; argv[4] = NULL;
	ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
	CHECK(el > 0);
	exe[el] = 0;
	fake_table[0] = (LspServer){ ".fake", "fake", argv, (const char *const *)roots, 1 };
	lsp_servers = fake_table;
	lsp_tap = tap;
	/* every sixth file holds what is not UTF-8 (and gets no more), every
	   sixth other one gets some typed */
	int bad = seed % 6 == 3;
	const char *init = seed % 6 == 0 ? "one\nλ tw\xE9x\x80\x80 𝓝\n\nthree ∀x\n" : "one\nλ two 𝓝\n\nthree ∀x\n";
	len = strlen(init);
	t = text_new(init, len);
	CHECK(t);
	for (size_t i = 0; i < len; i++) ids[i] = nextid++;
	for (int i = 0; i < nhist; i++) free(hist[i].gone);
	for (long i = 0; i < 2048; i++) { free(snap[i].text); free(snap[i].id); snap[i].text = NULL; snap[i].id = NULL; }
	nseen = nhist = 0;
	cur = 0;
	snapshot();
	/* A path past 512 bytes: its URI goes in every message (PR 27 review). */
	char path[700] = "/tmp/";
	for (int i = 0; i < 3; i++) { size_t k = strlen(path); memset(path + k, 'a', 200); path[k + 200] = 0; mkdir(path, 0700); strcat(path, "/"); }
	strcat(path, "homeros-test.fake");
	LspDoc *d = lsp_open(path, t);
	CHECK(d);
	long latest = -1;
	size_t asked[LSP_NASK] = { 0 };
	for (int step = 0; step < 250; step++) {
		unsigned a = rnd(100);
		if (step == 0 && seed % 3 == 0) {
			/* a change that joins bytes into one character: in a file
			   that held what is not UTF-8, deleting what stood between;
			   else typing a character's last byte after its first two */
			for (long long t0 = ms(); ms() - t0 < 2000; ) {   /* the server answering first */
				int n;
				loop_once(10);
				check_shown(d, &latest);
				if (lsp_diags(d, &n), n) break;
			}
			text_read(t, 0, buf, len);
			char *x = memmem(buf, len, "\xE9x", 2);
			size_t pos = x ? (size_t)(x - buf) : 0;
			if (seed % 6 == 0) edit(d, pos + 1, 1, "", 0);
			else {
				edit(d, pos, 0, "\xE2\x82" "abcdefghijklmnopqrstuvwxyz", 28);   /* a long line after it */
				lsp_flush(d);
				cur++;
				snapshot();
				usleep(50000);            /* what it says about this version waits */
				edit(d, pos + 2, 0, "\xAC", 1);
			}
			lsp_flush(d);
			cur++;
			snapshot();
			/* the server must count the joined character as one: ask what
			   stands after it, and wait for the answer */
			asked[0] = pos + 3;
			for (int k = 0; k < 20 && !lsp_answer(d, 0); k++) {   /* the server may refuse it */
				lsp_ask(d, 0, asked[0]);
				for (long long t0 = ms(); !lsp_answer(d, 0) && ms() - t0 < 300; ) { loop_once(10); check_shown(d, &latest); }
			}
			CHECK(lsp_answer(d, 0));
		} else if (step == 60) {
			/* the infoview's raw access (package 14), the server up */
			for (long long t0 = ms(); ms() - t0 < 1000; ) loop_once(10);
			lsp_restart(d);
			cur++;
			snapshot();
			for (long long t0 = ms(); ms() - t0 < 2000; ) {
				long l = -1;
				loop_once(10);
				check_shown(d, &l);
				if (l == cur) break;
			}
			const char *u = lsp_uri(d);
			CHECK(u && !strncmp(u, "file:///", 8) && strstr(u, "/homeros-test.fake"));
			size_t in;
			const char *ini = lsp_init(d, &in);
			CHECK(ini && in == ntapinit && !memcmp(ini, tapinit, in));
			Got g = { 0 }, gd = { 0 };
			CHECK(lsp_send("file:///nowhere", "$/test/echo", "{}", 2, got, &g) == -1);
			edit(d, 0, 0, "a", 1);            /* not flushed: lsp_send sends it first */
			CHECK(!lsp_send(u, "$/test/echo", "{}", 2, got, &g));
			cur++;
			snapshot();
			CHECK(tapver == cur);
			/* the status line: the server's name, and no work before any
			   $/progress (it reports only when asked here) */
			int w;
			const char *nm = lsp_name(d, &w);
			CHECK(nm && !strcmp(nm, strrchr(exe, '/') + 1) && w == -1);
			CHECK(!lsp_name(NULL, &w) && w == -1);
			long noted = nnoted;
			CHECK(!lsp_send(u, "$/test/work", "{}", 2, NULL, NULL));
			CHECK(!lsp_send(u, "$/test/note", "{}", 2, NULL, NULL));
			for (long long t0 = ms(); (!g.calls || nnoted == noted) && ms() - t0 < 2000; ) { loop_once(10); check_shown(d, &latest); }
			CHECK(g.calls == 1 && !g.null && g.ver == cur && nnoted == noted + 1);
			lsp_name(d, &w);
			CHECK(w == 1);
			CHECK(!lsp_send(u, "$/test/die", "{}", 2, got, &gd));   /* never answered: the server dies */
			for (long long t0 = ms(); !gd.calls && ms() - t0 < 2000; ) { loop_once(10); check_shown(d, &latest); }
			CHECK(gd.calls == 1 && gd.null);
			lsp_restart(d);
			lsp_name(d, &w);
			CHECK(w == -1);              /* a new server has said nothing yet */
			cur++;
			snapshot();
		} else if (step == 125) {
			/* one command of more edits than the log holds, the server
			   unheard meanwhile: what it said before must go, not be
			   mapped through the half of the edits the log kept.  The
			   server must be up (no didSave kills it from here; one sent
			   before has had a second), then a typed '@' makes it say one
			   thing about that version at once and nothing more until it
			   answers a request (the fake's marker).  The edits all go at
			   the start of the text, so a wrong mapping moves line 0 by
			   half as much, never by chance the right amount.  The answer
			   coming proves that the publication came before it. */
			for (long long t0 = ms(); ms() - t0 < 1000; ) loop_once(10);
			lsp_restart(d);
			cur++;
			snapshot();
			for (long long t0 = ms(); ms() - t0 < 2000; ) {
				long l = -1;
				loop_once(10);
				check_shown(d, &l);
				if (l == cur) break;
			}
			/* or, in the second such scenario, the '@' replaced: what it
			   said about the whole text, read only now, is mapped and its
			   end, inside the deletion, goes to the deletion's start; or,
			   in a file that is not UTF-8, replaced by the byte that
			   joins a character: what it said must go, not be mapped */
			edit(d, len, 0, seed % 3 ? "@" : "\xE2\x82@", seed % 3 ? 1 : 3);
			lsp_flush(d);
			long said = ++cur;
			snapshot();
			if (seed % 3 == 1) for (int k = 0; k < 4200; k++) edit(d, 0, 0, "a", 1);
			else edit(d, len - 1, 1, seed % 3 ? "xyz" : "\xAC", seed % 3 ? 3 : 1);
			lsp_flush(d);
			cur++;
			snapshot();
			asked[0] = 0;
			for (int k = 0; k < 20 && !lsp_answer(d, 0); k++) {   /* the server may refuse it */
				lsp_ask(d, 0, asked[0]);
				for (long long t0 = ms(); !lsp_answer(d, 0) && ms() - t0 < 300; ) { loop_once(10); check_shown(d, &latest); }
			}
			CHECK(lsp_answer(d, 0));
			int n;
			const LspDiag *g = lsp_diags(d, &n);
			CHECK(seed % 3 != 2 || n);
			for (int i = 0; i < n && seed % 3 == 0; i++) CHECK(atol(strchr(g[i].msg, ' ') + 1) != said);
			edit(d, len, 0, "!", 1);   /* typed after the answer: the fake's request must be answered by now */
			CHECK(!lsp_answer(d, 0));  /* the answer was for the text before it, though the edit is not sent yet */
			lsp_flush(d);
			cur++;
			snapshot();
		} else if (a < 35) {                              /* a command: a few edits */
			for (unsigned k = 0, n = 1 + rnd(3); k < n; k++) {
				size_t pos = charstart(rnd((unsigned)len + 1)), del = 0;
				for (unsigned c = rnd(3); c > 0 && pos + del < len; c--) {
					text_read(t, 0, buf, len);
					del += mstep((const unsigned char *)buf, len, pos + del);
				}
				const char *s = bad && !rnd(6) ? junk[rnd(5)] : alpha[rnd(10)];
				if (len > 2000) s = "";
				edit(d, pos, del, s, strlen(s));
			}
			lsp_flush(d);
			cur++;
			snapshot();
		} else if (a < 45) {
			int k = (int)rnd(LSP_NASK);
			asked[k] = charstart(rnd((unsigned)len + 1));
			lsp_ask(d, k, asked[k]);
		} else if (a < 46) {
			lsp_restart(d);
			cur++;
			snapshot();
		} else if (a < 47) {
			lsp_saved(d);
		} else loop_once((int)rnd(a < 60 ? 30 : 4));
		check_shown(d, &latest);
		for (int k = 0; k < LSP_NASK; k++) {
			const char *s = lsp_answer(d, k);
			if (!s) continue;
			char key[32];
			long v;
			int off;
			long cnt;
			size_t a0, z0;
			CHECK(sscanf(s, "%31s %ld %ld %zu %zu%n", key, &v, &cnt, &a0, &z0, &off) == 5);
			off++;
			text_read(t, 0, buf, len);
			size_t z = asked[k] < len ? asked[k] + mstep((const unsigned char *)buf, len, asked[k]) : asked[k];
			if (getenv("LSP_DEBUG")) fprintf(stderr, "answer %d at %zu [%s] text [%.*s]\n", k, asked[k], s, (int)len, buf);
			stat_answers++;
			char want[16];
			int wid[16];
			msent((const unsigned char *)buf + asked[k], z - asked[k], ids + asked[k], want, wid);
			CHECK(!strcmp(s + off, want));
		}
	}
	/* CatchUp: editing stops (the server may have died: once the loop has
	   seen it go, restart it) */
	for (long long t1 = ms(); ms() - t1 < 100; ) loop_once(10);
	lsp_restart(d);
	cur++;
	snapshot();
	long long t0 = ms();
	latest = -1;
	while (ms() - t0 < 2000) {
		loop_once(10);
		long l = -1;
		check_shown(d, &l);
		int n;
		lsp_diags(d, &n);
		if (n && l == cur) { latest = l; break; }
	}
	FILE *f = fopen(log, "r");
	char why[512];
	if (f && fgets(why, sizeof why, f)) { fputs(why, stderr); CHECK(!"the fake server saw a protocol violation"); }
	if (f) fclose(f);
	if (latest != cur) { int n; lsp_diags(d, &n); fprintf(stderr, "ver %ld latest %ld shown %d seen %d\n", cur, latest, n, nseen); }
	CHECK(latest == cur);
	lsp_close(d);
	for (long long t1 = ms(); ms() - t1 < 300; ) loop_once(20);
	text_free(t);
}

/* The goals window (lsp_goals): what Lean says at the cursor, drawn as
   lean.nvim draws it, after random moves and edits while answers come
   late and sessions go outdated; one question at a time (the server
   checks); the window opened on the right without taking the cursor, and
   \\i closing and opening it; lean.nvim's words while nothing is known
   and once the server died. */
static Buf *gwin_buf(Editor *e, int *nwin)
{
	Buf *b = NULL;
	*nwin = 0;
	for (Win *w = e->wins; w; w = w->next, ++*nwin) if (!w->buf->lsp) b = w->buf;
	return b;
}

static void gwant(Editor *e, Str *w)
{
	size_t l = e->win->line, n;
	const char *b = (const char *)core_line(e, l, &n);
	const char *x = memchr(b, '!', n);
	int done = n >= 4 && !memcmp(b, "done", 4), two = memmem(b, n, "\xCE\xBB", 2) != NULL;
	w->n = 0;
	if (done) str_lit(w, "Goals accomplished \xF0\x9F\x8E\x89");
	else if (!n) str_lit(w, "No goals.");
	if (n) {
		if (w->n) str_lit(w, "\n\n");
		if (two) str_lit(w, "\xE2\x96\xBC 2 goals\n");
		for (int k = 0; k < 1 + two; k++) {
			if (k) str_lit(w, "\n\n");
			str_lit(w, "case c\nh \xE2\x9C\x9Di : x\n\xE2\x8A\xA2 ");
			str_add(w, b, n);
		}
	}
	if (l % 2) str_addf(w, "%s\xE2\x96\xBC expected type (%zu:1-%zu:2)\n\xE2\x8A\xA2 T%zu", w->n ? "\n\n" : "", l + 1, l + 1,
	                    lsp_u16((const unsigned char *)b, e->win->col));
	if (x) str_addf(w, "%s\xE2\x96\xBC %zu:%zu-%zu:1: warning:\nbang", w->n ? "\n\n" : "", l + 1,
	                lsp_u16((const unsigned char *)b, (size_t)(x - b)) + 1, l + 2);
	size_t c = lsp_u16((const unsigned char *)b, n) + 1;
	if (memchr(b, '?', n)) str_addf(w, "%s\xE2\x96\xBC %zu:%zu-%zu:%zu: information:\nfar", w->n ? "\n\n" : "", l + 1, c, l + 1, c);
}

/* Waits until the window holds what w says (1), or 3 s pass (0). */
static int gsettle(Editor *e, Str *w, char *got, size_t *ng)
{
	int nwin;
	for (long long t0 = ms(); ms() - t0 < 3000; loop_once(10)) {
		lsp_goals(e);
		gwant(e, w);
		Buf *b = gwin_buf(e, &nwin);
		*ng = b ? text_read(b->text, 0, got, 1 << 15) : 0;
		if (*ng == w->n && !memcmp(got, w->s, w->n)) return 1;
	}
	return 0;
}

/* Waits for the window to show lean.nvim's text for the cursor, then checks
   lean.nvim's colours: hypothesis names Lean marked inserted, its diff mark
   on the goal, the goal prefix. */
static void gcheck(Editor *e, Str *w, char *got)
{
	size_t ng;
	int ok = gsettle(e, w, got, &ng), nwin;
	if (!ok) fprintf(stderr, "goals window:\n%.*s\n--- wanted:\n%.*s\n", (int)ng, got, (int)w->n, w->s);
	CHECK(ok);
	Buf *b = gwin_buf(e, &nwin);
	static unsigned char f[1 << 15];
	b->faces(b, 0, ng, f);
	char *h = memmem(got, ng, "case c\nh", 8), *t = memmem(got, ng, "\xE2\x8A\xA2 ", 4);
	if (h) CHECK(f[h - got + 7] == FACE_DIFF_PLUS && f[h - got + 9] == FACE_DIFF_PLUS && f[h - got] == FACE_KEYWORD);
	if (h && t) CHECK(f[t - got] == FACE_OPERATOR && (t[4] == '\n' || t + 4 == got + ng || f[t - got + 4] == FACE_DIFF_DELTA));
}

static void goals(const char *log)
{
	static char exe[4096], *const roots[] = { "root.mark", NULL };
	static const char *argv[] = { exe, "--fake", "goals", NULL, NULL };
	CHECK(!system("rm -rf '" GROOT "' && mkdir -p '" GROOT "/sub' && touch '" GROOT "/root.mark'"));
	static LspServer table[2];
	static char got[1 << 15];
	argv[3] = log;
	ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
	CHECK(el > 0);
	exe[el] = 0;
	table[0] = (LspServer){ ".fake", "fake", argv, (const char *const *)roots, 1 };
	lsp_servers = table;
	lsp_tap = NULL;
	Editor e;
	CHECK(!core_open(&e, GROOT "/sub/x.fake") && e.buf->lsp);
	e.rows = 40; e.cols = 160;
	const char *init = "one\n\n\xCE\xBB x \xF0\x9D\x93\x9D\ndone it!\nfin?\n";
	CHECK(!core_edit(&e, 0, 0, init, strlen(init)));
	lsp_flush(e.buf->lsp);
	Win *lean = e.win;
	lsp_goals(&e);
	int nwin;
	Buf *b = gwin_buf(&e, &nwin);
	/* nothing known yet: lean.nvim's on_processing */
	CHECK(b && nwin == 2 && e.win == lean && e.buf == lean->buf && b->nonumber);
	CHECK(text_len(b->text) == 18 && text_read(b->text, 0, got, 18) == 18 && !memcmp(got, "Processing file...", 18));
	Str w = { 0 };
	size_t ng = 0;
	Win *iv = lean->next ? lean->next : e.wins;
	CHECK(iv->buf == b && iv->x > lean->x && iv->w == 160 / 3);   /* botright, a third of the width */
	/* every kind of line once: none, one and two goals, done, an expected
	   type away from the line start */
	for (e.win->line = 0; e.win->line < 5; e.win->line++) {
		e.win->col = e.win->line == 3 ? 2 : 0;
		gcheck(&e, &w, got);
	}
	/* "fin?" said nothing, without a version: its message goes */
	e.win->line = 4;
	ng = text_read(gwin_buf(&e, &nwin)->text, 0, got, 1 << 15);
	CHECK(memmem(got, ng, "far", 3));
	CHECK(!lsp_send(lsp_uri(e.buf->lsp), "$/test/plain", "{}", 2, NULL, NULL));
	for (long long t0 = ms(); ms() - t0 < 3000 && memmem(got, ng, "far", 3); loop_once(10)) {
		lsp_goals(&e);
		ng = text_read(gwin_buf(&e, &nwin)->text, 0, got, 1 << 15);
	}
	CHECK(!memmem(got, ng, "far", 3));
	static const char *const ins[] = { "a", "\xCE\xBB", "!", "\n", "done ", "\xF0\x9D\x93\x9D", "?", "" };
	for (int step = 0; step < 300; step++) {
		unsigned a = rnd(100);
		size_t lines = core_lines(&e) - 1;          /* the last line stays empty: the text ends in a newline */
		if (a < 40) {
			e.win->line = rnd((unsigned)lines);
			size_t n, c = rnd(8);
			const unsigned char *l = core_line(&e, e.win->line, &n);
			for (e.win->col = 0; c-- && e.win->col < n; ) e.win->col += cplen(l[e.win->col]);
		} else if (a < 60) {
			const char *s = ins[rnd(8)];
			size_t at = core_line_start(&e, e.win->line) + e.win->col;
			if (!*s && at + 1 < text_len(e.buf->text)) {
				size_t n;
				const unsigned char *l = core_line(&e, e.win->line, &n);
				CHECK(!core_edit(&e, at, e.win->col < n ? cplen(l[e.win->col]) : 1, "", 0));
			} else CHECK(!core_edit(&e, at, 0, s, strlen(s)));
			core_clamp(&e);
		} else if (a < 62) {
			lsp_goals_toggle(&e);
			CHECK(gwin_buf(&e, &nwin) == NULL && nwin == 1 && e.win == lean);
			lsp_goals_toggle(&e);
			CHECK(gwin_buf(&e, &nwin) && nwin == 2 && e.win == lean);
		} else loop_once((int)rnd(20));
		if (e.win->line >= core_lines(&e) - 1 && e.win->line) e.win->line--, e.win->col = 0;
		lsp_flush(e.buf->lsp);
		lsp_goals(&e);
		CHECK(e.win == lean);
		if (step % 50 == 49) {
			gcheck(&e, &w, got);
		}
	}
	/* the server dies: lean.nvim's tombstone; started again, goals again */
	CHECK(!lsp_send(lsp_uri(e.buf->lsp), "$/test/die", "{}", 2, NULL, NULL));
	const char *dead = "\xF0\x9F\xAA\xA6 The Lean language server is dead.";
	for (long long t0 = ms(); ms() - t0 < 1000; loop_once(10)) {
		lsp_goals(&e);
		ng = text_read(gwin_buf(&e, &nwin)->text, 0, got, 1 << 15);
		if (ng == strlen(dead) && !memcmp(got, dead, ng)) break;
	}
	CHECK(ng == strlen(dead) && !memcmp(got, dead, ng));
	/* nothing from the new server yet: nothing from the old one shows */
	lsp_restart(e.buf->lsp);
	lsp_goals(&e);
	CHECK(text_read(gwin_buf(&e, &nwin)->text, 0, got, 1 << 15) == 18 && !memcmp(got, "Processing file...", 18));
	gcheck(&e, &w, got);
	free(w.s);
	core_free(&e);
	CHECK(!system("rm -rf '" GROOT "'"));
	for (long long t1 = ms(); ms() - t1 < 300; ) loop_once(20);
}

static void units(void)
{
	/* the kernel against whole code points, on random valid and invalid bytes */
	unsigned char s[64];
	for (int r = 0; r < 200000; r++) {
		size_t n = rnd(12);
		for (size_t i = 0; i < n; i++) s[i] = rnd(3) ? (unsigned char)"a\xC3\xA9\xE2\x82\xAC\xF0\x9D\x93\x9D\x80\xFF"[rnd(13)] : (unsigned char)rnd(256);
		size_t u = 0, i = 0;
		while (i < n) {
			size_t k = 1;
			unsigned c = s[i];
			size_t want = c < 0x80 ? 1 : c >= 0xC0 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF8 ? 4 : 0;
			if (want > 1 && i + want <= n) {
				k = want;
				for (size_t j = 1; j < want; j++) if ((s[i + j] & 0xC0) != 0x80) k = 1;
			}
			for (size_t c16 = u; c16 < u + (k == 4 ? 2 : 1); c16++) CHECK(lsp_u16_byte(s, n, c16) == i);
			u += k == 4 ? 2 : 1;
			i += k;
		}
		CHECK(lsp_u16(s, n) == u);
		CHECK(lsp_u16_byte(s, n, u) == n && lsp_u16_byte(s, n, u + 5) == n);
	}
	/* what goes to the server: the model's U+FFFD for what is not UTF-8,
	   so its UTF-16 count is the kernel's (with 4-byte forms past U+10FFFF) */
	Jtok *qt = NULL;
	int qcap = 0;
	for (int r = 0; r < 200000; r++) {
		size_t n = rnd(12);
		for (size_t i = 0; i < n; i++) s[i] = rnd(3) ? (unsigned char)"a\xC3\xA9\xED\xA0\x80\xF4\x90\xC0\xFF\""[rnd(12)] : (unsigned char)rnd(256);
		Str q = { 0 };
		str_lit(&q, "\"");
		CHECK(str_addq(&q, (const char *)s, n) >= 0);
		str_lit(&q, "\"");
		char got[256], want[256];
		int wid[256], sid[64] = { 0 };
		CHECK(json_parse(q.s, q.n, &qt, &qcap) == 1);
		size_t gn = json_str(q.s, qt, 0, got);
		size_t wn = msent(s, n, sid, want, wid);
		CHECK(gn == wn && !memcmp(got, want, gn));
		free(q.s);
	}
	free(qt);
	/* JSON: what the client reads */
	Jtok *tk = NULL;
	int cap = 0;
	const char *j = "{\"a\":[1,{\"b\":\"x\\u00e9\\ud835\\udcdd\\n\"}],\"c\":true,\"d\":null}";
	CHECK(json_parse(j, strlen(j), &tk, &cap) == 11);
	char o[64];
	int b = json_get(j, tk, json_at(tk, json_get(j, tk, 0, "a"), 1), "b");
	CHECK(json_str(j, tk, b, o) == 8 && !strcmp(o, "xé𝓝\n"));
	CHECK(json_true(j, tk, json_get(j, tk, 0, "c")) && !json_true(j, tk, json_get(j, tk, 0, "d")));
	CHECK(json_int(j, tk, json_at(tk, json_get(j, tk, 0, "a"), 0)) == 1);
	const char *bad[] = { "", "{", "[1,]", "{\"a\"}", "\"\\x\"", "01", "1.", "-", "[1 2]", "{\"a\":1,}", "\"\t\"", "tru", "\"\\u12\"" };
	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(json_parse(bad[i], strlen(bad[i]), &tk, &cap) < 0);
	free(tk);
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "--fake")) return fake(argv[2], argv[3]);
	units();
	char log[] = "/tmp/homeros-lsp-XXXXXX";
	int fd = mkstemp(log);
	CHECK(fd >= 0);
	close(fd);
	int n = getenv("LSP_N") ? atoi(getenv("LSP_N")) : 12;
	unsigned long long seed = getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : (unsigned long long)time(NULL);
	printf("test_lsp: %d scenarios, SEED=%llu\n", n, seed);
	rng ^= seed * 0x9E3779B97F4A7C15ULL;
	for (int i = 0; i < n; i++) {
		scenario((int)rnd(1u << 27) * 6 + i % 6, log);   /* each kind of file in turn (seed % 6) */
		FILE *f = fopen(log, "r");
		char line[512];
		if (f && fgets(line, sizeof line, f)) { fputs(line, stderr); CHECK(!"the fake server saw a protocol violation"); }
		if (f) fclose(f);
	}
	goals(log);
	FILE *f = fopen(log, "r");
	char line[512];
	if (f && fgets(line, sizeof line, f)) { fputs(line, stderr); CHECK(!"the fake server saw a protocol violation"); }
	if (f) fclose(f);
	unlink(log);
	printf("test_lsp: ok: %ld diagnostics shown for the current version, %ld for an older one, %ld checks after edits, %ld answers\n", stat_new, stat_old, stat_moved, stat_answers);
	return 0;
}
