/* The phase 2 gate (DESIGN.md, Plan): on a long Lean file, typing
   continuously must never hide diagnostics for more than 2 s.  LspSync's
   "simple" rules failed it, so the client has "ok"'s edit log; this shows
   both on the same logs.  The file measured: gen_lean.py > LongSorry.lean.

   stale FILE.lean [ms per key] [seconds] [line]
   copies FILE into a scratch Lake project, opens it in the LSP client with
   `lake serve` behind a logging proxy (this program run as --proxy), waits
   for the first full elaboration, then types at the start of the given line
   (default: the middle) one key every [ms per key] for [seconds], each key
   its own command (one didChange), as Helix sends.  It types " simp" and
   deletes it again, inside a proof, over and over.

   Hidden time: the client ignores diagnostics published for a version it
   has already left (LspSync "simple").  For each publication, the time
   until the client next shows one is how long what the server knew stayed
   off the screen; the gate is its maximum.  The proxy sees both
   directions, so a publication counts as shown when its version is the
   latest didChange the proxy has passed on (the client flushes each key at
   once, so it leaves no edit unsent; hypothesis: the two disagree only
   within the pipe's latency). */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "loop.h"
#include "lsp.h"
#include "../json.h"
#include "text.h"
#include "bench.h"

int core_changed;                     /* core.h's, which this program does not link */

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* --- the proxy: forwards both ways, logs each frame's kind and version --- */

static FILE *lg;
static Jtok *tk;
static int cap;

static void note(char dir, const char *m, size_t n)
{
	if (json_parse(m, n, &tk, &cap) < 0) return;
	int me = json_get(m, tk, 0, "method"), p = json_get(m, tk, 0, "params");
	char name[64] = "";
	if (me >= 0 && tk[me].z - tk[me].a < sizeof name) json_str(m, tk, me, name);
	if (!strcmp(name, "textDocument/didChange") || !strcmp(name, "textDocument/didOpen"))
		fprintf(lg, "%lld C %ld\n", now(), json_int(m, tk, json_get(m, tk, json_get(m, tk, p, "textDocument"), "version")));
	else if (!strcmp(name, "textDocument/publishDiagnostics")) {
		int d = json_get(m, tk, p, "diagnostics");
		fprintf(lg, "%lld S %ld %d %d\n", now(), json_int(m, tk, json_get(m, tk, p, "version")),
		        d >= 0 ? tk[d].n : 0, json_true(m, tk, json_get(m, tk, p, "isIncremental")));
	}
	(void)dir;
}

/* One direction: whole frames from a to b, logged. */
typedef struct { int from, to; char dir; Str in; } Pipe;

static int pump(Pipe *p)
{
	char b[1 << 16];
	ssize_t r = read(p->from, b, sizeof b);
	if (r <= 0) return -1;
	str_add(&p->in, b, (size_t)r);
	for (;;) {
		char *e = strstr(p->in.s, "\r\n\r\n");
		if (!e) return 0;
		size_t h = (size_t)(e - p->in.s) + 4, len = strtoul(strchr(p->in.s, ':') + 1, NULL, 10);
		if (p->in.n - h < len) return 0;
		note(p->dir, p->in.s + h, len);
		for (size_t w = 0; w < h + len; ) {
			ssize_t k = write(p->to, p->in.s + w, h + len - w);
			if (k <= 0) return -1;
			w += (size_t)k;
		}
		memmove(p->in.s, p->in.s + h + len, p->in.n - h - len);
		p->in.n -= h + len;
		p->in.s[p->in.n] = 0;
	}
}

static int proxy(const char *log, char **argv)
{
	int up[2], down[2];
	if (pipe(up) || pipe(down) || !(lg = fopen(log, "w"))) return 2;
	setvbuf(lg, NULL, _IOLBF, 0);
	pid_t pid = fork();
	if (!pid) {
		dup2(up[0], 0); dup2(down[1], 1);
		close(up[0]); close(up[1]); close(down[0]); close(down[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(up[0]); close(down[1]);
	Pipe c = { 0, up[1], 'C', { 0 } }, s = { down[0], 1, 'S', { 0 } };
	for (;;) {
		struct pollfd pf[2] = { { 0, POLLIN, 0 }, { down[0], POLLIN, 0 } };
		if (poll(pf, 2, -1) < 0) continue;
		if (pf[0].revents && pump(&c)) break;
		if (pf[1].revents && pump(&s)) break;
	}
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	return 0;
}

/* --- the client ------------------------------------------------------------ */

/* Real Lean as an oracle for the mapping: its "declaration uses 'sorry'"
   warning covers the theorem's name, so wherever the client shows one,
   mapped or not, the text there must be a name tNNN. */
static LspDoc *doc;
static long sorry_ok, sorry_bad;

static void check_sorry(void)
{
	int n;
	const LspDiag *g = lsp_diags(doc, &n);
	for (int i = 0; i < n; i++) {
		if (!g[i].msg || !strstr(g[i].msg, "declaration uses")) continue;
		char b[16] = "";
		size_t k = g[i].z - g[i].a < sizeof b - 1 ? g[i].z - g[i].a : sizeof b - 1;
		b[text_read(t, g[i].a, b, k)] = 0;
		int good = b[0] == 't' && b[1] >= '0' && b[1] <= '9';
		for (size_t j = 1; j < k; j++) good = good && b[j] >= '0' && b[j] <= '9';
		if (good) sorry_ok++;
		else { sorry_bad++; fprintf(stderr, "sorry warning on \"%s\"\n", b); }
	}
}

static void run_for(long long ms)
{
	for (long long end = now() + ms; now() < end; ) {
		loop_once((int)(end - now()));
		if (core_changed && doc) { core_changed = 0; check_sorry(); }
	}
}


/* The proxy's log replayed under both of LspSync's rules: "simple" shows
   a publication only for the version the client is at, "ok" (the edit
   log) any not older than the last one shown.  Typing runs from the first
   didChange to the last. */
static void report(const char *log)
{
	static long long at[1 << 20], hid[1 << 20];
	static long ver[1 << 20], cur[1 << 20];
	int n = 0;
	long c = -1, v, cnt;
	int inc;
	long long t, start = -1, stop = 0;
	char kind;
	FILE *f = fopen(log, "r");
	while (f && n < (1 << 20) && fscanf(f, "%lld %c %ld", &t, &kind, &v) == 3) {
		if (kind == 'C') { c = v; if (v > 0 && start < 0) start = t; stop = t; continue; }
		if (fscanf(f, "%ld %d", &cnt, &inc) != 2) break;
		at[n] = t; ver[n] = v; cur[n] = c; n++;
	}
	if (f) fclose(f);
	static char ok[1 << 20];
	for (int rule = 0; rule < 2; rule++) {
		int nh = 0, shown = 0;
		long floor = -1;
		long long first = -1, next = -1;
		for (int i = 0; i < n; i++) {
			ok[i] = rule ? ver[i] >= floor : ver[i] == cur[i];
			if (rule && ok[i]) floor = ver[i];
			if (ok[i] && at[i] >= start) { shown++; if (at[i] > stop && first < 0) first = at[i]; }
		}
		/* hidden: from each publication after typing began to the next one shown */
		for (int i = n - 1; i >= 0; i--) {
			if (ok[i]) next = at[i];
			if (at[i] >= start) hid[nh++] = (next < 0 ? at[n - 1] : next) - at[i];
		}
		qsort(hid, (size_t)nh, sizeof *hid, cmpl);
		printf("%-6s %d publications, %d shown; hidden per publication: median %lld ms, max %lld ms; first shown after the last key %lld ms\n",
		       rule ? "ok" : "simple", nh, shown, nh ? hid[nh / 2] : 0, nh ? hid[nh - 1] : 0, first < 0 ? -1 : first - stop);
		printf("GATE %s %s (max hidden %lld ms, limit 2000 ms)\n", rule ? "ok" : "simple",
		       nh && hid[nh - 1] > 2000 ? "FAIL" : "pass", nh ? hid[nh - 1] : 0);
	}
}

int main(int argc, char **argv)
{
	if (argc > 3 && !strcmp(argv[1], "--proxy")) return proxy(argv[2], argv + 3);
	if (argc == 3 && !strcmp(argv[1], "--replay")) { report(argv[2]); return 0; }
	if (argc < 2) { fprintf(stderr, "usage: stale FILE.lean [ms per key] [seconds] [line] | stale --replay LOG\n"); return 2; }
	int per = argc > 2 ? atoi(argv[2]) : 150, secs = argc > 3 ? atoi(argv[3]) : 30;
	char dir[] = "/tmp/homeros-stale-XXXXXX", path[4200], log[4200], self[4096];
	if (!mkdtemp(dir)) return 1;
	snprintf(path, sizeof path, "%s/Long.lean", dir);
	snprintf(log, sizeof log, "%s/proxy.log", dir);
	FILE *in = fopen(argv[1], "rb"), *out = fopen(path, "wb");
	static char src[1 << 24];
	size_t n = in ? fread(src, 1, sizeof src, in) : 0;
	if (!in || !out || !n) { fprintf(stderr, "stale: cannot read %s\n", argv[1]); return 1; }
	fwrite(src, 1, n, out);
	fclose(in); fclose(out);
	char lake[4300];
	snprintf(lake, sizeof lake, "%s/lakefile.toml", dir);
	out = fopen(lake, "w");
	fputs("name = \"bench\"\n\n[[lean_lib]]\nname = \"Long\"\n", out);
	fclose(out);
	ssize_t sl = readlink("/proc/self/exe", self, sizeof self - 1);
	if (sl <= 0) return 1;
	self[sl] = 0;
	static const char *pargv[8];
	static const char *const roots[] = { "lakefile.toml", NULL };
	pargv[0] = self; pargv[1] = "--proxy"; pargv[2] = log; pargv[3] = "lake"; pargv[4] = "serve"; pargv[5] = NULL;
	static LspServer table[2];
	table[0] = (LspServer){ ".lean", "lean4", pargv, roots, 1 };
	lsp_servers = table;

	t = text_new(src, n);
	LspDoc *d = doc = lsp_open(path, t);
	if (!d) { fprintf(stderr, "stale: no server\n"); return 1; }
	/* the first full elaboration: until file progress has been empty for 3 s */
	long long t0 = now(), quiet = now();
	int np;
	while (now() - t0 < 600000) {
		run_for(100);
		lsp_progress(d, &np);
		if (np) quiet = now();
		else if (now() - quiet > 3000 && now() - t0 > 3000) break;
	}
	int nd;
	lsp_diags(d, &nd);
	printf("elaborated in %.1f s, %d diagnostics shown\n", (double)(quiet - t0) / 1000, nd);

	/* type inside a proof: after the first "by" line at or below the line asked */
	size_t line = argc > 4 ? (size_t)atol(argv[4]) : text_lines(t) / 2, pos = 0;
	for (size_t l = line; l < text_lines(t); l++) {
		char b[512];
		size_t a = text_line_start(t, l), k = text_read(t, a, b, sizeof b - 1);
		b[k] = 0;
		char *by = strstr(b, ":= by\n");
		if (by) { pos = text_line_start(t, l + 1); line = l + 1; break; }
	}
	/* "  simp" typed key by key as the start of the proof, then deleted,
	   key by key: the proof is broken while it is typed (errors appear and
	   go), and every key is a new version */
	const char *word = "  simp\n";
	long long start = now(), next = start;
	int k = 0, keys = 0;
	size_t wl = strlen(word);
	fprintf(stderr, "typing at line %zu, %d ms per key, %d s\n", line + 1, per, secs);
	while (now() - start < secs * 1000LL) {
		run_for(next - now());
		next += per;
		if (k < (int)wl) edit(d, pos + (size_t)k, 0, word + k, 1);
		else edit(d, pos + (size_t)(2 * wl - k - 1), 1, "", 0);
		k = (k + 1) % (int)(2 * wl);
		keys++;
	}
	long long stop = now();
	/* catch up: until progress is quiet again */
	quiet = now();
	while (now() - quiet < 3000 && now() - stop < 300000) {
		run_for(100);
		lsp_progress(d, &np);
		if (np) quiet = now();
	}
	lsp_close(d);
	run_for(500);

	printf("%d keys; sorry warnings checked against the text: %ld on a theorem name, %ld elsewhere\n", keys, sorry_ok, sorry_bad);
	report(log);
	text_free(t);
	return 0;
}
