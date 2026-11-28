/* The pickers, as my-nvim's snacks.picker (folke/snacks.nvim at 882c996):
   SPC ff smart (open buffers, recent files, then the files fd or rg lists),
   SPC fg live grep (rg), SPC , buffers, and the dashboard's files and
   recent.  One picker at a time, drawn as snacks' float
   over the windows: a prompt, the list, a preview.  The comments name the
   snacks file each rule comes from. */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tools.h"
#include "core.h"
#include "dash.h"
#include "loop.h"
#include "make.h"
#include "match.h"
#include "term.h"
#include "shada.h"
#include "snip.h"
#include "sql.h"
#include "str.h"
#include "uni.h"

/* An item: its text (what the matcher sees) and file in the arena, the
   position to jump to, the buffer it stands for. */
typedef struct {
	size_t text, tlen, file, flen;
	long line, col;            /* 1-based line, 0-based byte column; line 0: none */
	int buf;
	int recent;                /* from the recent source: frecency seeds from its mtime */
	double score;
	double frecency;           /* -1: not looked up yet */
	unsigned idx;
} Item;

enum { PROMPT = 256, PREVIEW = 128, LIMIT_LIVE = 10000 };
static struct {
	int open, what, insert;    /* insert: the prompt is in insert mode */
	int g;                     /* a g typed in normal mode: gg */
	char prompt[PROMPT];
	size_t plen, pcur;
	char cwd[PATH_MAX];
	size_t cwdlen;
	char *a; size_t an, acap;  /* the arena */
	Item *it; size_t nit, itcap;
	unsigned *shown; size_t nshown, shcap;
	size_t sel, top;           /* the cursor in the list, the first row shown */
	Match m;
	int proc, gen, done;       /* the running finder, its generation, finished */
	Str part;                  /* a line not yet ended */
	/* the preview: lines of the selected item's file around its line */
	char pv[PREVIEW][256]; int pvn, pvfirst;
	long pvfor;                /* the item index it shows, -1 none */
	Editor *e;
	double now;                /* the frecency's clock, read once per picker */
} P = { .proc = -1, .pvfor = -1 };

/* an absolute path: /x, or on Windows C:\x, C:/x or \\server */
static int absolute(const char *f)
{
#ifdef _WIN32
	if (f[0] == '\\' || (f[0] && f[1] == ':' && (f[2] == '\\' || f[2] == '/'))) return 1;
#endif
	return f[0] == '/';
}

static int sep(char c)
{
#ifdef _WIN32
	if (c == '\\') return 1;
#endif
	return c == '/';
}

/* path made absolute against the cwd (fnamemodify ":p", without resolving);
   returns its length, at most n - 1 */
static size_t full(const char *path, char *out, size_t n)
{
#ifdef _WIN32
	/* GetFullPathNameW: '\' only, as v:oldfiles spells them */
	if (n >= PATH_MAX && !file_full(path, out)) return strlen(out);
#endif
	int k = absolute(path) ? snprintf(out, n, "%s", path) : snprintf(out, n, "%s/%s", P.cwd, path);
	return k < 0 ? 0 : (size_t)k < n ? (size_t)k : n - 1;
}

static void layout(Editor *e, Box *outer, Box *list, Box *pv, int *wide);
static void preview(void);

static int put(const char *s, size_t n, size_t *at)
{
	if (P.an + n > P.acap) {
		size_t c = P.acap ? P.acap * 2 : 1 << 16;
		while (c < P.an + n) c *= 2;
		char *a = realloc(P.a, c);
		if (!a) return -1;
		P.a = a; P.acap = c;
	}
	memcpy(P.a + P.an, s, n);
	*at = P.an;
	P.an += n;
	return 0;
}

/* sort.lua's default with config/defaults.lua's fields: score:desc, #text, idx */
static int cmp(const void *x, const void *y)
{
	const Item *a = &P.it[*(const unsigned *)x], *b = &P.it[*(const unsigned *)y];
	if (a->score != b->score) return a->score > b->score ? -1 : 1;
	if (a->tlen != b->tlen) return a->tlen < b->tlen ? -1 : 1;
	return a->idx < b->idx ? -1 : a->idx > b->idx;
}

/* Is it sorted?  Live grep is not, and an empty prompt sorts only for
   smart (matcher.sort_empty). */
static int sorting(void) { return P.what != PICK_GREP && (P.m.ng || P.what == PICK_SMART); }

/* core/frecency.lua: visits decay with a 30-day half-life; a score is kept
   as the time it decays to 1 (its "deadline"), in a file of
   "deadline path" lines ($XDG_DATA_HOME/homeros/picker-frecency, as snacks'
   stdpath("data")/snacks/picker-frecency). */
static ShadaLine *fr;   /* t: the deadline */
static size_t nfr, frcap;
static FileId frid;
static int frloaded;
#define LAMBDA (log(2.0) / (30.0 * 24 * 3600))

static void fr_file(char *f, size_t n) { file_stdpath(f, n, 0, "/picker-frecency"); }

static double *fr_find(const char *path, size_t n, int make)
{
	for (size_t i = 0; i < nfr; i++)
		if (strlen(fr[i].path) == n && !memcmp(fr[i].path, path, n)) return &fr[i].t;
	if (!make) return NULL;
	if (nfr == frcap) {
		size_t c = frcap ? frcap * 2 : 64;
		void *t = realloc(fr, c * sizeof *fr);
		if (!t) return NULL;
		fr = t; frcap = c;
	}
	if (!(fr[nfr].path = malloc(n + 1))) return NULL;
	memcpy(fr[nfr].path, path, n);
	fr[nfr].path[n] = 0;
	return &fr[nfr++].t;
}

static void fr_load(void)
{
	char f[PATH_MAX];
	frloaded = 1;
	fr_file(f, sizeof f);
	shada_load(f, &fr, &nfr, &frid);   /* what it could read */
	frcap = nfr;
}

/* M:visit: one more visit to an absolute path, saved at once */
static void fr_visit(const char *path, size_t n)
{
	double now = (double)time(NULL), *d = fr_find(path, n, 0);
	double sc = (d ? exp(LAMBDA * (*d - now)) : 0) + 1;
	if (!d && !(d = fr_find(path, n, 1))) return;
	*d = now + log(sc) / LAMBDA;
	char f[PATH_MAX];
	fr_file(f, sizeof f);
	shada_save(f, fr, nfr, &frid);
}

/* M:get: the stored score, or a seed (M:seed): for a buffer from when it
   was last used (here: now), for a recent file from its mtime */
static double frecency(Item *i, double now)
{
	char abs[2 * PATH_MAX];
	const char *f = P.a + i->file;
	size_t n = i->flen;
	/* the arena's paths are not NUL-ended: stat wants a copy */
	n = (size_t)(absolute(f) ? snprintf(abs, sizeof abs, "%.*s", (int)n, f) : snprintf(abs, sizeof abs, "%s/%.*s", P.cwd, (int)n, f));
	f = abs;
	double *d = fr_find(f, n, 0);
	struct stat st;
	if (d) return exp(LAMBDA * (*d - now));
	if (i->recent) return stat(f, &st) ? 0 : exp(-LAMBDA * (now - (double)st.st_mtime));
	return i->buf ? 1 : 0;
}

/* M:update with the smart picker's frecency and cwd_bonus */
static double score(Item *i)
{
	int s = match_score(&P.m, P.a + i->text, i->tlen, i->flen ? P.a + i->file : NULL, i->flen);
	double r = s;
	if (s && P.what == PICK_SMART && i->flen) {
		if (i->frecency < 0) i->frecency = frecency(i, P.now);
		r += (1 - 1 / (1 + i->frecency)) * 8;
		if (!absolute(P.a + i->file) || (i->flen > P.cwdlen && !memcmp(P.a + i->file, P.cwd, P.cwdlen) && sep(P.a[i->file + P.cwdlen])))
			r += 10;
	}
	return i->score = r;
}

static void show(unsigned k)
{
	if (P.nshown == P.shcap) {
		size_t c = P.shcap ? P.shcap * 2 : 1024;
		unsigned *s = realloc(P.shown, c * sizeof *s);
		if (!s) return;
		P.shown = s; P.shcap = c;
	}
	P.shown[P.nshown++] = k;
}

/* Matches everything again (M:run): the cursor goes back to the top. */
static void rematch(void)
{
	P.nshown = 0;
	for (size_t k = 0; k < P.nit; k++) if (score(&P.it[k]) > 0) show((unsigned)k);
	if (sorting()) qsort(P.shown, P.nshown, sizeof *P.shown, cmp);
	P.sel = P.top = 0;
}

/* Do two paths, each absolute or relative to cwd, name the same file? */
static int same(const char *a, size_t an, const char *b, size_t bn)
{
	if (absolute(a) == absolute(b)) return an == bn && !memcmp(a, b, an);
	if (absolute(b)) { const char *t = a; a = b; b = t; size_t n = an; an = bn; bn = n; }
	return an == P.cwdlen + 1 + bn && !memcmp(a, P.cwd, P.cwdlen) && sep(a[P.cwdlen]) && !memcmp(a + P.cwdlen + 1, b, bn);
}

static int add(const char *text, size_t tn, const char *file, size_t fn, long line, long col, int buf, int recent)
{
	if (P.nit == P.itcap) {
		size_t c = P.itcap ? P.itcap * 2 : 1024;
		Item *t = realloc(P.it, c * sizeof *t);
		if (!t) return -1;
		P.it = t; P.itcap = c;
	}
	Item *i = &P.it[P.nit];
	*i = (Item){ .line = line, .col = col, .buf = buf, .recent = recent, .idx = (unsigned)P.nit + 1, .frecency = -1 };
	if (put(text, tn, &i->text)) return -1;
	i->tlen = tn;
	if (file && put(file, fn, &i->file)) return -1;
	i->flen = file ? fn : 0;
	/* transform unique_file: a file listed once, as its first item (smart):
	   the buffers, then the recent files, come before the files fd lists */
	if (P.what == PICK_SMART && file && !buf)
		for (size_t k = 0; k < P.nit && (P.it[k].buf || P.it[k].recent); k++)
			if (same(P.a + P.it[k].file, P.it[k].flen, file, fn)) { P.an = i->text; return 0; }
	P.nit++;
	if (score(i) > 0) show((unsigned)P.nit - 1);
	return 0;
}

static void finder_stop(void)
{
	if (P.proc >= 0) proc_kill(P.proc);
	P.proc = -1;
	P.gen++;
	P.part.n = 0;
}

/* One line of a finder's output (source/files.lua, source/grep.lua). */
static void item(const char *s, size_t n)
{
	if (P.what != PICK_GREP) { add(s, n, s, n, 0, 0, 0, 0); return; }
	/* rg -0: file NUL line:col:text.  The search names "." because rg
	   searches a piped stdin (ours is a pipe; snacks' is /dev/null), so
	   every file starts "./". */
	if (n > 2 && s[0] == '.' && s[1] == '/') s += 2, n -= 2;
	const char *z = memchr(s, 0, n);
	if (!z) return;
	size_t fn = (size_t)(z - s);
	const char *r = z + 1, *e = s + n;
	long ln = match_num(&r, e);
	if (ln < 0 || r == e || *r++ != ':') return;
	long col = match_num(&r, e);
	if (col < 0 || r == e || *r != ':') return;
	/* the text is "file:line:col:text" */
	char t[4096];
	size_t tn = (size_t)snprintf(t, sizeof t, "%.*s:%.*s", (int)fn, s, (int)(e - z - 1), z + 1);
	if (tn >= sizeof t) tn = sizeof t - 1;
	add(t, tn, s, fn, ln, col - 1, 0, 0);
}

/* the live grep stops at LIMIT_LIVE items */
static void line(void *ctx, char *s, size_t n)
{
	(void)ctx;
	if (P.done) return;
	item(s, n);
	if (P.what == PICK_GREP && P.nit >= LIMIT_LIVE) { finder_stop(); P.done = 1; }
}

static void on_out(void *ctx, int stream, const char *b, size_t n)
{
	if ((intptr_t)ctx != P.gen || !P.open) return;
	if (stream == 0) { P.proc = -1; P.done = 1; core_changed = 1; return; }
	if (stream != 1) return;
	size_t before = P.nshown;
	str_lines(&P.part, b, n, line, NULL);
	if (P.nshown != before) {
		if (sorting()) qsort(P.shown, P.nshown, sizeof *P.shown, cmp);
		preview();
		core_changed = 1;
	}
}

static int spawn(char *const argv[])
{
	P.done = 0;
	P.proc = proc_spawn(argv, P.cwd, on_out, (void *)(intptr_t)P.gen);
	return P.proc;
}

/* source/files.lua: the first of fd, fdfind, rg and find there is.  It
   means to skip find on Windows, where find.exe searches text, but tests
   has("win-32"), which is never set; this skips it. */
static void files(void)
{
	static char *cmd[][12] = {
		{ "fd", "--type", "f", "--type", "l", "--color", "never", "-E", ".git", NULL },
		{ "fdfind", "--type", "f", "--type", "l", "--color", "never", "-E", ".git", NULL },
		{ "rg", "--files", "--no-messages", "--color", "never", "-g", "!.git", NULL },
#ifndef _WIN32
		{ "find", ".", "-type", "f", "-not", "-path", "*/.git/*", "-not", "-path", "*/.*", NULL },
#endif
	};
	for (size_t i = 0; i < sizeof cmd / sizeof *cmd; i++) if (spawn(cmd[i]) >= 0) return;
	P.done = 1;
#ifdef _WIN32
	core_msg(P.e, "No supported finder found: `fd` `fdfind` `rg`");
#else
	core_msg(P.e, "No supported finder found: `fd` `fdfind` `rg` `find`");
#endif
}

/* util.parse: "pattern -- rg args" (^(.-)%s+%-%-%s*(.*)$); the args,
   blanks squeezed, split at spaces outside quotes (the quotes stay, and a
   quote of the other kind inside one ends it, as in snacks).  s is
   rewritten in place. */
enum { GREP_ARGS = 32 };
static int parse(char *s, char **pat, char **args)
{
	char *d = NULL;
	for (char *q = s; (q = strstr(q, "--")); q++)
		if (q > s && match_space(q[-1])) { d = q; break; }
	*pat = s;
	if (!d) return 0;
	char *e = d;
	while (e > s && match_space(e[-1])) e--;
	*e = 0;
	while (match_space(**pat)) ++*pat;                     /* vim.trim */
	/* squeeze and trim the args into d */
	char *w = d;
	for (const char *r = d + 2; *r; r++)
		if (!match_space(*r)) *w++ = *r;
		else if (w > d && !match_space(r[1]) && r[1]) *w++ = ' ';
	*w = 0;
	int n = 0;
	char quote = 0, *a = d;
	for (char *c = d;; c++) {
		if (*c == '\'' || *c == '"') quote = quote == *c ? 0 : *c;
		else if (!*c || (*c == ' ' && !quote)) {
			int end = !*c;
			*c = 0;
			if (n < GREP_ARGS && (*a || !end)) args[n++] = a;
			if (end) return n;
			a = c + 1;
		}
	}
}

/* source/grep.lua, live: the prompt is rg's pattern, then its arguments */
static void grep(void)
{
	char buf[PROMPT + 1], *pat, *pa[GREP_ARGS];
	memcpy(buf, P.prompt, P.plen);
	buf[P.plen] = 0;
	int na = parse(buf, &pat, pa);
	finder_stop();
	P.nit = P.nshown = 0;
	P.an = 0;
	P.sel = P.top = 0;
	if (!P.plen) { P.done = 1; return; }     /* need_search */
	char *argv[16 + GREP_ARGS + 4] = { "rg", "--color=never", "--no-heading", "--with-filename", "--line-number",
		"--column", "--smart-case", "--max-columns=500", "--max-columns-preview",
		"--glob=!.bare", "--glob=!.git", "-0", "--no-hidden" };
	int k = 13;
	for (int i = 0; i < na; i++) argv[k++] = pa[i];
	argv[k++] = "--"; argv[k++] = pat; argv[k++] = "."; argv[k] = NULL;
	if (spawn(argv) < 0) { P.done = 1; core_msg(P.e, "No supported finder found: `rg`"); }
}

const char *path_rel(const char *cwd, size_t k, const char *f, size_t *n)
{
	if (*n > k && !memcmp(f, cwd, k) && sep(f[k])) { *n -= k + 1; return f + k + 1; }
	return f;
}

/* source/buffers.lua: listed buffers, last used first */
static void buffers(Editor *e)
{
	for (Buf *b = e->bufs; b; b = b->next) {
		char t[3 * PATH_MAX], name[2 * PATH_MAX];
		/* Neovim's buffer name is the full path */
		if (!b->path) strcpy(name, "[Scratch]");
		else full(b->path, name, sizeof name);
		/* util.text(item, { "buf", "name", "filetype", "buftype" }), with no
		   filetype known here */
		int n = snprintf(t, sizeof t, "%d %s  ", b->nr, b->path ? name : "");
		add(t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1), name, strlen(name), 0, 0, b->nr, 0);
	}
}

/* source/recent.lua: this session's buffers last used first, then
   v:oldfiles; each once, if it exists, not the current file and not in
   stdpath data or state (the filter's paths; homeros has no cache) */
static void recent(Editor *e)
{
	char cur[2 * PATH_MAX] = "", f[2 * PATH_MAX], skip[2][PATH_MAX];
	enum { MAXB = 256 };
	const Buf *bs[MAXB];
	int nb = 0;
	for (const Buf *b = e->bufs; b && nb < MAXB; b = b->next) {
		if (!b->path) continue;
		/* table.sort by lastused, newest first (stable here) */
		int j = nb++;
		for (; j && bs[j - 1]->lastused < b->lastused; j--) bs[j] = bs[j - 1];
		bs[j] = b;
	}
	if (e->buf->path) full(e->buf->path, cur, sizeof cur);
	file_stdpath(skip[0], sizeof skip[0], 0, "");
	file_stdpath(skip[1], sizeof skip[1], 1, "");
	size_t first = P.nit;
	for (size_t i = 0;; i++) {
		const char *o = i < (size_t)nb ? NULL : shada_oldfile(i - (size_t)nb);
		if (i >= (size_t)nb && !o) break;
		size_t n = o ? (size_t)snprintf(f, sizeof f, "%s", o) : full(bs[i]->path, f, sizeof f);
		struct stat st;
		int want = n < sizeof f && strcmp(f, cur) && stat(f, &st) == 0;
		for (int k = 0; k < 2 && want; k++) {
			size_t sn = strlen(skip[k]);
			want = !(!strncmp(f, skip[k], sn) && (!f[sn] || sep(f[sn])));
		}
		for (size_t k = first; k < P.nit && want; k++) want = !same(P.a + P.it[k].file, P.it[k].flen, f, n);
		if (want) add(f, n, f, n, 0, 0, 0, 1);
	}
}

/* SPC i s (package 19's, agreed by thread 0): the snippets that match the
   prompt in the completion popup's order (snip_get, one order for both);
   each item's name is followed in the arena by a 0, its description and
   its body, each ended by a 0.  :Connect (package 22's, agreed): the
   connections and their servers' databases (sql_pick, a live one with
   its mark), matched as they come */
static void snippets(void)
{
	const char *d, *b, *n;
	size_t at;
	int sql = P.what == PICK_SQL;
	P.nit = P.nshown = P.an = 0;
	P.sel = P.top = 0;
	P.pvfor = -1;
	while ((n = sql ? sql_pick(P.e, (int)P.nit) : snip_get(P.e, P.prompt, P.plen, (int)P.nit, &d, &b)) && !add(n, strlen(n), NULL, 0, 0, 0, 0, 0)
	       && (sql || (!put("", 1, &at) && !put(d, strlen(d) + 1, &at) && !put(b, strlen(b) + 1, &at)))) {}
}

int tools_pick(Editor *e, int what)
{
	if (what == PICK_REGISTERS || what == PICK_EXPLORER) {
		core_msg(e, "picker not built yet");
		return -1;
	}
	finder_stop();
	P.e = e;
	P.open = 1; P.what = what; P.insert = 1; P.g = 0;
	P.plen = P.pcur = 0;
	P.nit = P.nshown = P.an = 0;
	P.sel = P.top = 0;
	P.pvfor = -1;
	P.done = 1;
	if (!getcwd(P.cwd, sizeof P.cwd)) strcpy(P.cwd, "/");
	P.cwdlen = strlen(P.cwd);
	P.m.filename_bonus = 1;
	match_init(&P.m, "", 0);
	P.now = (double)time(NULL);
	if (!frloaded) {
		/* M.setup: the store, and a visit to each buffer open now */
		fr_load();
		for (Buf *b = e->bufs; b; b = b->next) {
			char f[2 * PATH_MAX];
			struct stat st;
			if (!b->path) continue;
			size_t n = full(b->path, f, sizeof f);
			if (!stat(f, &st)) fr_visit(f, n);
		}
	}
	if (what == PICK_GREP) return 0;
	if (what == PICK_SNIPPETS || what == PICK_SQL) snippets();
	if (what == PICK_SMART || what == PICK_BUFFERS) buffers(e);
	if (what == PICK_SMART || what == PICK_RECENT) recent(e);
	if (what == PICK_SMART || what == PICK_FILES) files();
	preview();
	return 0;
}

static void close_picker(void)
{
	finder_stop();
	P.open = 0;
}

/* The prompt changed: match again, or for grep run rg again. */
static void changed(void)
{
	if (P.what == PICK_GREP) { grep(); return; }
	match_init(&P.m, P.prompt, P.plen);
	if (P.what == PICK_SNIPPETS) snippets();
	else rematch();
}

/* actions.lua's jump: open the item's file (or buffer) at its position */
static void confirm(Editor *e)
{
	if (!P.nshown) { close_picker(); return; }
	Item it = P.it[P.shown[P.sel]];
	if (P.what == PICK_SNIPPETS) {
		close_picker();
		snip_insert(e, P.a + it.text);
		return;
	}
	if (P.what == PICK_SQL) {
		char c[600];
		snprintf(c, sizeof c, "%.*s", (int)it.tlen, P.a + it.text);
		close_picker();
		sql_connect(e, c);
		return;
	}
	char f[PATH_MAX];
	snprintf(f, sizeof f, "%.*s", (int)it.flen, P.a + it.file);
	long line = it.line, col = it.col;
	/* a file:line:col typed into the prompt (matcher.file) */
	if (!line && P.m.line) { line = P.m.line; col = P.m.col; }
	close_picker();
	if (it.buf ? core_buf_goto(e, it.buf) : core_edit_file(e, f, 0)) return;
	/* BufWinEnter's visit (frecency.lua) */
	char abs[2 * PATH_MAX];
	fr_visit(abs, full(f, abs, sizeof abs));
	if (line) {
		core_goto(e, (size_t)line - 1, (size_t)col);
		core_scroll_cursor(e, 'z');    /* norm! zzzv */
	}
}

static void move(long d)
{
	if (!P.nshown) return;
	long s = (long)P.sel + d, n = (long)P.nshown;
	/* layout.cycle: past either end wraps for one step */
	if (d == 1 || d == -1) s = (s + n) % n;
	P.sel = (size_t)(s < 0 ? 0 : s >= n ? n - 1 : s);
}

static int is_word(int c) { return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }

/* The prompt's insert-mode keys (snacks' win.input.keys, then Vim's own). */
static int edit(int k)
{
	if (k == KEY_BS) {
		if (!P.pcur) return 0;
		size_t p = uni_prev((const unsigned char *)P.prompt, P.plen, P.pcur);
		memmove(P.prompt + p, P.prompt + P.pcur, P.plen - P.pcur);
		P.plen -= P.pcur - p; P.pcur = p;
		return 1;
	}
	if (k == 23) {                     /* CTRL-W: the word before the cursor */
		size_t p = P.pcur;
		while (p && P.prompt[p - 1] == ' ') p--;
		if (p && is_word((unsigned char)P.prompt[p - 1])) while (p && is_word((unsigned char)P.prompt[p - 1])) p--;
		else if (p) p--;
		memmove(P.prompt + p, P.prompt + P.pcur, P.plen - P.pcur);
		P.plen -= P.pcur - p; P.pcur = p;
		return 1;
	}
	if (k == KEY_LEFT) { if (P.pcur) P.pcur = uni_prev((const unsigned char *)P.prompt, P.plen, P.pcur); return 0; }
	if (k == KEY_RIGHT) { if (P.pcur < P.plen) P.pcur += uni_len((const unsigned char *)P.prompt, P.plen, P.pcur); return 0; }
	if (k < ' ' || k == 127 || k >= 0x110000) return 0;
	char u[4];
	size_t n = (size_t)uni_put((unsigned char *)u, (unsigned)k);
	if (P.plen + n > PROMPT) return 0;
	memmove(P.prompt + P.pcur + n, P.prompt + P.pcur, P.plen - P.pcur);
	memcpy(P.prompt + P.pcur, u, n);
	P.plen += n; P.pcur += n;
	return 1;
}

static int list_height(Editor *e);

static int key(Editor *e, int k);

int tools_key(Editor *e, int k)
{
	if (term_key(e, k)) return 1;
	if (!P.open) return make_key(e, k) || dash_key(e, k);
	key(e, k);
	if (P.open) preview();
	return 1;
}

static int key(Editor *e, int k)
{
	int h = list_height(e);
	/* the key after g ends it, whatever it is (g<CR> is no command) */
	if (P.g) { P.g = 0; if (k == 'g') P.sel = 0; return 1; }
	switch (k) {
	case KEY_ENTER: confirm(e); return 1;
	case KEY_DOWN: case 14: case 10: move(1); return 1;    /* Down CTRL-N CTRL-J */
	case KEY_UP: case 16: case 11: move(-1); return 1;     /* Up CTRL-P CTRL-K */
	case 4: move(h / 2); return 1;                          /* CTRL-D list_scroll_down */
	case 21: move(-(h / 2)); return 1;                      /* CTRL-U list_scroll_up */
	case KEY_CTRL_C: close_picker(); return 1;
	}
	if (P.insert) {
		if (k == KEY_ESC) { P.insert = 0; if (P.pcur) P.pcur = uni_prev((const unsigned char *)P.prompt, P.plen, P.pcur); return 1; }
		if (edit(k)) changed();
		return 1;
	}
	/* normal mode in the prompt */
	switch (k) {
	case KEY_ESC: case 'q': close_picker(); break;
	case 'j': move(1); break;
	case 'k': move(-1); break;
	case 'G': if (P.nshown) P.sel = P.nshown - 1; break;
	case 'g': P.g = 1; break;
	case 'i': P.insert = 1; break;
	case 'a': P.insert = 1; if (P.pcur < P.plen) P.pcur += uni_len((const unsigned char *)P.prompt, P.plen, P.pcur); break;
	case 'I': P.insert = 1; P.pcur = 0; break;
	case 'A': P.insert = 1; P.pcur = P.plen; break;
	case 'h': edit(KEY_LEFT); break;
	case 'l': if (P.pcur < P.plen) { size_t n = P.pcur + uni_len((const unsigned char *)P.prompt, P.plen, P.pcur); if (n < P.plen) P.pcur = n; } break;
	case '0': P.pcur = 0; break;
	case '$': P.pcur = P.plen ? uni_prev((const unsigned char *)P.prompt, P.plen, P.plen) : 0; break;
	}
	return 1;
}

/* The layouts (config/layouts.lua): "default" from 120 columns (list left,
   preview right), else "vertical" (preview below). */
static void layout(Editor *e, Box *outer, Box *list, Box *pv, int *wide)
{
	int rows = e->rows, cols = e->cols;
	*wide = cols >= 120;
	int w = *wide ? cols * 8 / 10 : cols / 2, h = rows * 8 / 10;
	if (*wide && w < 120) w = 120;
	if (!*wide && w < 80) w = 80;
	if (!*wide && h < 30) h = 30;
	if (w > cols) w = cols;
	if (h > rows - 1) h = rows - 1;
	*outer = (Box){ (rows - 1 - h) / 2, (cols - w) / 2, h, w };
	if (*wide) {
		int pw = w / 2;
		*list = (Box){ outer->y, outer->x, h, w - pw };
		*pv = (Box){ outer->y, outer->x + w - pw, h, pw };
	} else {
		int ph = h * 4 / 10;
		*list = (Box){ outer->y, outer->x, h - ph, w };
		*pv = (Box){ outer->y + h - ph, outer->x, ph, w };
	}
}

/* rows of items: the list box without its borders and the prompt with its line */
static int list_height(Editor *e)
{
	Box o, l, p;
	int wide;
	layout(e, &o, &l, &p, &wide);
	int h = l.h - 4;
	return h > 1 ? h : 1;
}

static void hline(Grid *g, int r, int x, int w, const char *l, const char *m, const char *rt)
{
	grid_put(g, r, x, (const unsigned char *)l, strlen(l), 1, ATTR_NONE);
	for (int c = x + 1; c < x + w - 1; c++) grid_put(g, r, c, (const unsigned char *)m, strlen(m), 1, ATTR_NONE);
	grid_put(g, r, x + w - 1, (const unsigned char *)rt, strlen(rt), 1, ATTR_NONE);
}

void box_border(Grid *g, Box b, const char *title)
{
	hline(g, b.y, b.x, b.w, "╭", "─", "╮");
	for (int r = b.y + 1; r < b.y + b.h - 1; r++) {
		grid_put(g, r, b.x, (const unsigned char *)"│", 3, 1, ATTR_NONE);
		grid_put(g, r, b.x + b.w - 1, (const unsigned char *)"│", 3, 1, ATTR_NONE);
	}
	hline(g, b.y + b.h - 1, b.x, b.w, "╰", "─", "╯");
	if (title) grid_puts(g, b.y, b.x + (b.w - (int)strlen(title)) / 2, g->cols, title, strlen(title), ATTR_NONE, FACE_NONE);
}

/* Reads the preview of the selected item (preview.lua's file previewer):
   the lines of its file from just above its line.  Called by whatever moved
   the cursor, so tools_draw only copies them. */
static void preview(void)
{
	Editor *e = P.e;
	Box o, lb, pb;
	int wide;
	layout(e, &o, &lb, &pb, &wide);
	int h = pb.h - 2;
	long want = P.nshown ? (long)P.shown[P.sel] : -1;
	if (want == P.pvfor) return;
	P.pvfor = want; P.pvn = 0; P.pvfirst = 1;
	if (want < 0 || P.what == PICK_SQL) return;
	Item *it = &P.it[want];
	if (P.what == PICK_SNIPPETS) {
		/* a snippet: its body, after its name and description */
		const char *b = P.a + it->text + it->tlen + 1;
		b += strlen(b) + 1;
		for (int k; *b && P.pvn < PREVIEW && P.pvn < h; b += k + (b[k] == '\n')) {
			k = (int)strcspn(b, "\n");
			snprintf(P.pv[P.pvn++], sizeof P.pv[0], "%.*s", k, b);
		}
		return;
	}
	if (it->buf) {
		/* a buffer: its own text */
		Buf *b = e->bufs;
		while (b && b->nr != it->buf) b = b->next;
		if (!b) return;
		for (size_t l = 0; l < text_lines(b->text) && P.pvn < PREVIEW && P.pvn < h; l++) {
			size_t a = text_line_start(b->text, l), z = text_line_start(b->text, l + 1) - 1;
			size_t k = text_read(b->text, a, P.pv[P.pvn], z - a < sizeof P.pv[0] - 1 ? z - a : sizeof P.pv[0] - 1);
			P.pv[P.pvn++][k] = 0;
		}
		return;
	}
	char f[PATH_MAX];
	snprintf(f, sizeof f, "%.*s", (int)it->flen, P.a + it->file);
	FILE *fp = fopen(f, "r");
	if (!fp) return;
	long first = it->line > h / 2 ? it->line - h / 2 : 1, l = 1;
	char s[4096];
	while (P.pvn < PREVIEW && P.pvn < h && fgets(s, sizeof s, fp)) {
		size_t k = strcspn(s, "\n");
		int whole = s[k] == '\n' || feof(fp);
		if (l >= first) { snprintf(P.pv[P.pvn], sizeof P.pv[0], "%.*s", (int)k, s); P.pvn++; }
		if (whole) l++;
	}
	P.pvfirst = (int)first;
	fclose(fp);
}

static void pick_draw(Editor *e, Grid *g)
{
	dash_draw(e, g);
	if (!P.open) return;
	static const char *title[] = { " Smart ", " Grep ", " Buffers ", "", "",  " Files ", " Recent ", " Snippets ", " Connections " };
	Box o, l, p;
	int wide, h = list_height(e);
	layout(e, &o, &l, &p, &wide);
	grid_blank(g, o.y, o.x, o.h, o.w);
	box_border(g, l, title[P.what]);
	box_border(g, p, NULL);
	/* the prompt, its line, and the count at its right (input.lua) */
	int x = grid_puts(g, l.y + 1, l.x + 2, g->cols, "> ", 2, ATTR_NONE, FACE_NONE);
	int px = grid_puts(g, l.y + 1, x, l.x + l.w - 1, P.prompt, P.pcur, ATTR_NONE, FACE_NONE);
	grid_puts(g, l.y + 1, px, l.x + l.w - 1, P.prompt + P.pcur, P.plen - P.pcur, ATTR_NONE, FACE_NONE);
	char cnt[48];
	snprintf(cnt, sizeof cnt, "%zu/%zu ", P.nshown, P.nit);
	grid_puts(g, l.y + 1, l.x + l.w - 1 - (int)strlen(cnt), g->cols, cnt, strlen(cnt), ATTR_DIM, FACE_NONE);
	hline(g, l.y + 2, l.x, l.w, "├", "─", "┤");
	/* the list, keeping the cursor in view */
	if (P.sel < P.top) P.top = P.sel;
	if (P.sel >= P.top + (size_t)h) P.top = P.sel - (size_t)h + 1;
	for (int r = 0; r < h && P.top + (size_t)r < P.nshown; r++) {
		size_t k = P.top + (size_t)r;
		Item *it = &P.it[P.shown[k]];
		int row = l.y + 3 + r, a = k == P.sel ? ATTR_REV : ATTR_NONE, end = l.x + l.w - 1, c = l.x + 1;
		if (a) for (int i = c; i < end; i++) grid_put(g, row, i, (const unsigned char *)" ", 1, 1, a);
		if (P.what == PICK_BUFFERS) {
			char nr[16];
			snprintf(nr, sizeof nr, "%d ", it->buf);
			c = grid_puts(g, row, c + 1, end, nr, strlen(nr), a ? a : ATTR_DIM, FACE_NONE);
		}
		/* format "file": the directory dimmed, then the name */
		size_t fn = it->flen ? it->flen : it->tlen;
		const char *f = path_rel(P.cwd, P.cwdlen, P.a + (it->flen ? it->file : it->text), &fn);
		size_t d = fn;
		while (d && f[d - 1] != '/') d--;
		c = grid_puts(g, row, c + 1, end, f, d, a ? a : ATTR_DIM, FACE_NONE);
		c = grid_puts(g, row, c, end, f + d, fn - d, a, FACE_NONE);
		if (P.what == PICK_SNIPPETS) {
			/* and its description, dimmed */
			const char *d = P.a + it->text + it->tlen + 1;
			grid_puts(g, row, c + 1, end, d, strlen(d), a ? a : ATTR_DIM, FACE_NONE);
		}
		if (it->line) {
			/* grep: :line:col: and the line */
			const char *t = P.a + it->text + it->flen + 1;
			size_t tn = it->tlen - it->flen - 1;
			c = grid_puts(g, row, c, end, ":", 1, a, FACE_NONE);
			grid_puts(g, row, c, end, t, tn, a, FACE_NONE);
		}
	}
	/* the preview */
	for (int r = 0; r < P.pvn && r < p.h - 2; r++)
		grid_puts(g, p.y + 1 + r, p.x + 1, p.x + p.w - 1, P.pv[r], strlen(P.pv[r]), ATTR_NONE, FACE_NONE);
	g->crow = l.y + 1;
	g->ccol = px;
}

/* the terminal over everything, the picker too */
void tools_draw(Editor *e, Grid *g)
{
	pick_draw(e, g);
	term_draw(e, g);
}


/* The file manager's buffer comes with the core's oil:// hooks; until
   then it refuses, as ref/tools.c does. */
int tools_oil(Editor *e, const char *dir) { (void)dir; core_msg(e, "tools: file manager not built yet"); return -1; }
int tools_oil_key(Editor *e, int key) { (void)e; (void)key; return 0; }
int tools_write(Editor *e, Buf *b) { (void)b; core_msg(e, "tools: file manager not built yet"); return -1; }
