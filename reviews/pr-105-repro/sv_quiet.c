/* SystemVerilog phase 1's gate (the plan's "errors as you type"): with
   slang-server behind the client, an error saved with :w shows within
   200 ms, and LspSync "ok" holds against a real server.

   sv FILE.sv [runs] [line] [text]
   opens FILE (inside its project, which slang-server indexes) with the
   client's own table (slang-server on PATH), then, runs times: types
   text (default "@@@ ", a syntax error; an undeclared name gives an
   elaboration error) at the start of the line (default: the middle) and
   10 spaces at the file's start, one each 20 ms, and times until an error
   shows inside the text; takes both out; types it again, writes the file and
   sends didSave (as :w does) and times until an error shows;
   then types 10 bytes at the file's start, which the client must map the
   shown error across before the server answers (LspSync "ok": it stays
   inside the typed text); then takes all of it out, saves, and times until no error shows.
   Every error shown while the text is in is checked to sit inside it.  The file
   is written back as it was.  Prints the times (median, range) and the
   server's peak memory (VmHWM). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "loop.h"
#include "lsp.h"
#include "text.h"

int core_changed;                     /* core.h's, which this program does not link */

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static Text *t;
static const char *path;
static long bad;                      /* errors shown off "@" while one is typed */
static const char *typo = "@@@ ";     /* what is typed */
static size_t at, len;                /* where it is, or (size_t)-1 */

static void edit(LspDoc *d, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	lsp_edit(d, t, pos, ndel, ins, nins);
	text_delete(t, pos, ndel);
	text_insert(t, pos, ins, nins);
	lsp_flush(d);
}

static void save(LspDoc *d)
{
	static char b[1 << 16];
	FILE *f = fopen(path, "wb");
	for (size_t i = 0, k; f && (k = text_read(t, i, b, sizeof b)); i += k) fwrite(b, 1, k, f);
	if (f) fclose(f);
	lsp_saved(d);
}

static int errors(LspDoc *d)
{
	int n, e = 0;
	const LspDiag *v = lsp_diags(d, &n);
	for (int i = 0; i < n; i++) {
		if (v[i].sev != 1) continue;
		e++;
		if (at != (size_t)-1 && v[i].a >= at && v[i].a < at + len) continue;
		if (at != (size_t)-1) { bad++; fprintf(stderr, "error off what was typed: [%zu, %zu) %s\n", v[i].a, v[i].z, v[i].msg); }
	}
	return e;
}

/* until want says yes, at most 10 s; the time in µs, -1 never */
static long long until(LspDoc *d, int want_err)
{
	long long t0 = now();
	while (now() - t0 < 10000000) {
		loop_once(10);
		if (core_changed) { core_changed = 0; if ((errors(d) > 0) == want_err) return now() - t0; }
	}
	return -1;
}

static long hwm(void)
{
	DIR *p = opendir("/proc");
	long kb = -1;
	for (struct dirent *e; p && (e = readdir(p)); ) {
		char f[300], line[256];
		snprintf(f, sizeof f, "/proc/%s/status", e->d_name);
		FILE *s = fopen(f, "r");
		int ours = 0;
		while (s && fgets(line, sizeof line, s)) {
			if (!strncmp(line, "Name:\tslang-server", 18)) ours = 1;
			if (ours && !strncmp(line, "VmHWM:", 6)) kb = atol(line + 6);
		}
		if (s) fclose(s);
	}
	if (p) closedir(p);
	return kb;
}

static int cmpl(const void *a, const void *b) { long long x = *(const long long *)a, y = *(const long long *)b; return (x > y) - (x < y); }
static void show(const char *what, long long *v, int n)
{
	qsort(v, (size_t)n, sizeof *v, cmpl);
	printf("%s: median %.1f ms, range %.1f to %.1f ms (%d runs)\n", what, v[n / 2] / 1000.0, v[0] / 1000.0, v[n - 1] / 1000.0, n);
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: sv FILE.sv [runs] [line]\n"); return 2; }
	path = argv[1];
	int runs = argc > 2 ? atoi(argv[2]) : 5;
	if (argc > 4) typo = argv[4];
	len = strlen(typo);
	static char src[1 << 24];
	FILE *in = fopen(path, "rb");
	size_t n = in ? fread(src, 1, sizeof src, in) : 0;
	if (!in || !n) { fprintf(stderr, "sv: cannot read %s\n", path); return 1; }
	fclose(in);
	t = text_new(src, n);
	at = (size_t)-1;
	long long t0 = now();
	LspDoc *d = lsp_open(path, t);
	if (!d) { fprintf(stderr, "sv: no server\n"); return 1; }
	/* the first publication, then 2 s of quiet */
	while (!core_changed && now() - t0 < 60000000) loop_once(100);
	printf("first diagnostics after %.0f ms, %d errors\n", (now() - t0) / 1000.0, errors(d));
	for (long long q = now(); now() - q < 2000000; ) loop_once(100);
	core_changed = 0;
	size_t line = argc > 3 ? (size_t)atol(argv[3]) - 1 : text_lines(t) / 2;
	long long typed_ms[64], saved_ms[64], gone_ms[64];
	int k = 0;
	for (; k < runs && k < 64; k++) {
		size_t pos = text_line_start(t, line);
		/* typed, then 10 spaces before it, one each 20 ms, each its own
		   didChange: what the server says about a version the client has
		   left is mapped (LspSync "ok"), or, without a version, taken as
		   for the latest (Neovim's rule; slang-server sends none) */
		at = pos;
		core_changed = 0;
		long long t0 = now(), shown = -1;
		edit(d, pos, 0, typo, len);
		for (int i = 0; i < 10; i++) {
			edit(d, 0, 0, " ", 1);
			at++;
			for (long long q = now(); now() - q < 20000; ) {
				loop_once(5);
				if (core_changed) { core_changed = 0; if (errors(d) && shown < 0) shown = now() - t0; }
			}
		}
		typed_ms[k] = shown >= 0 ? shown : until(d, 1) + now() - t0;
		at = (size_t)-1;
		edit(d, pos + 10, len, "", 0);
		edit(d, 0, 10, "", 0);
		until(d, 0);
		/* review of PR 105: 1 s of quiet first, so no publication for the
		   text of the step before is still on its way when the time starts */
		for (long long q = now(); now() - q < 1000000; ) loop_once(50);
		if (errors(d)) fprintf(stderr, "review: an error is held before anything is typed\n");
		/* typed and saved at once, as "i@@@ <Esc>:w" */
		at = pos;
		core_changed = 0;
		edit(d, pos, 0, typo, len);
		save(d);
		saved_ms[k] = until(d, 1);
		/* typed before it: the shown error must follow "@" at once */
		edit(d, 0, 0, "/*mapped*/", 10);
		at += 10;
		errors(d);
		for (long long q = now(); now() - q < 500000; ) { loop_once(10); if (core_changed) { core_changed = 0; errors(d); } }
		/* taken out and saved: the client drops the error with its text at
		   once; the time is until the server says so too */
		at = (size_t)-1;
		edit(d, pos + 10, len, "", 0);
		edit(d, 0, 10, "", 0);
		core_changed = 0;
		save(d);
		gone_ms[k] = until(d, 0);
		if (typed_ms[k] < 0 || saved_ms[k] < 0 || gone_ms[k] < 0) { fprintf(stderr, "sv: run %d timed out\n", k); break; }
	}
	FILE *out = fopen(path, "wb");
	if (out) { fwrite(src, 1, n, out); fclose(out); }
	if (k) { show("error shown after typing", typed_ms, k); show("error shown after :w", saved_ms, k); show("server's clean answer after :w", gone_ms, k); }
	printf("errors shown off the typed text: %ld; slang-server peak memory %ld MB\n", bad, hwm() / 1024);
	lsp_close(d);
	return bad || k < runs;
}
