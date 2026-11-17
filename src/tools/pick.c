/* The pickers, as my-nvim's snacks.picker (folke/snacks.nvim at 882c996):
   SPC ff smart (open buffers, then the files fd or rg lists), SPC fg live
   grep (rg), SPC , buffers.  One picker at a time, drawn as snacks' float
   over the windows: a prompt, the list, a preview.  The comments name the
   snacks file each rule comes from. */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "api.h"
#include "core.h"
#include "loop.h"
#include "match.h"
#include "uni.h"

/* An item: its text (what the matcher sees) and file in the arena, the
   position to jump to, the buffer it stands for. */
typedef struct {
	size_t text, tlen, file, flen;
	long line, col;            /* 1-based line, 0-based byte column; line 0: none */
	int buf;
	double score;
	double frecency;           /* -1: not looked up yet */
	unsigned idx;
} Item;

enum { PROMPT = 256, PREVIEW = 128, LIMIT_LIVE = 10000 };
typedef struct { int y, x, h, w; } Box;

static struct {
	int open, what, insert;    /* insert: the prompt is in insert mode */
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
	char part[4096]; size_t npart;   /* a line not yet ended */
	/* the preview: lines of the selected item's file around its line */
	char pv[PREVIEW][256]; int pvn, pvfirst;
	long pvfor;                /* the item index it shows, -1 none */
	Editor *e;
	double now;                /* the frecency's clock, read once per picker */
} P = { .proc = -1, .pvfor = -1 };

int tools_dirty;

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
static struct { char *path; double deadline; } *fr;
static size_t nfr, frcap;
static int frloaded;
#define LAMBDA (log(2.0) / (30.0 * 24 * 3600))

static void fr_file(char *f, size_t n)
{
	const char *d = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
	if (d && *d) snprintf(f, n, "%s/homeros/picker-frecency", d);
	else snprintf(f, n, "%s/.local/share/homeros/picker-frecency", h ? h : "");
}

static double *fr_find(const char *path, size_t n, int make)
{
	for (size_t i = 0; i < nfr; i++)
		if (strlen(fr[i].path) == n && !memcmp(fr[i].path, path, n)) return &fr[i].deadline;
	if (!make) return NULL;
	if (nfr == frcap) {
		size_t c = frcap ? frcap * 2 : 64;
		void *t = realloc(fr, c * sizeof *fr);
		if (!t) return NULL;
		fr = t; frcap = c;
	}
	if (!(fr[nfr].path = strndup(path, n))) return NULL;
	return &fr[nfr++].deadline;
}

static void fr_load(void)
{
	char f[PATH_MAX], ln[PATH_MAX + 64];
	frloaded = 1;
	fr_file(f, sizeof f);
	FILE *fp = fopen(f, "r");
	if (!fp) return;
	while (fgets(ln, sizeof ln, fp)) {
		char *sp = strchr(ln, ' ');
		if (!sp) continue;
		double *d = fr_find(sp + 1, strcspn(sp + 1, "\n"), 1);
		if (d) *d = strtod(ln, NULL);
	}
	fclose(fp);
}

/* M:visit: one more visit to an absolute path, saved at once */
static void fr_visit(const char *path, size_t n)
{
	double now = (double)time(NULL), *d = fr_find(path, n, 0);
	double sc = (d ? exp(LAMBDA * (*d - now)) : 0) + 1;
	if (!d && !(d = fr_find(path, n, 1))) return;
	*d = now + log(sc) / LAMBDA;
	char f[PATH_MAX], tmp[PATH_MAX + 8];
	fr_file(f, sizeof f);
	snprintf(tmp, sizeof tmp, "%s.new", f);
	char *slash = strrchr(f, '/');
	*slash = 0;
	mkdir(f, 0700);
	*slash = '/';
	FILE *fp = fopen(tmp, "w");
	if (!fp) return;
	for (size_t i = 0; i < nfr; i++) fprintf(fp, "%.17g %s\n", fr[i].deadline, fr[i].path);
	if (!fclose(fp)) rename(tmp, f);
}

/* M:get: the stored score, or for a buffer a seed from when it was last
   used (here: now, a buffer's last use is not kept) */
static double frecency(Item *i, double now)
{
	char abs[2 * PATH_MAX];
	const char *f = P.a + i->file;
	size_t n = i->flen;
	if (f[0] != '/') { n = (size_t)snprintf(abs, sizeof abs, "%s/%.*s", P.cwd, (int)n, f); f = abs; }
	double *d = fr_find(f, n, 0);
	if (d) return exp(LAMBDA * (*d - now));
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
		if (P.a[i->file] != '/' || (i->flen > P.cwdlen && !memcmp(P.a + i->file, P.cwd, P.cwdlen) && P.a[i->file + P.cwdlen] == '/'))
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
	if ((a[0] == '/') == (b[0] == '/')) return an == bn && !memcmp(a, b, an);
	if (b[0] == '/') { const char *t = a; a = b; b = t; size_t n = an; an = bn; bn = n; }
	return an == P.cwdlen + 1 + bn && !memcmp(a, P.cwd, P.cwdlen) && a[P.cwdlen] == '/' && !memcmp(a + P.cwdlen + 1, b, bn);
}

static int add(const char *text, size_t tn, const char *file, size_t fn, long line, long col, int buf)
{
	if (P.nit == P.itcap) {
		size_t c = P.itcap ? P.itcap * 2 : 1024;
		Item *t = realloc(P.it, c * sizeof *t);
		if (!t) return -1;
		P.it = t; P.itcap = c;
	}
	Item *i = &P.it[P.nit];
	*i = (Item){ .line = line, .col = col, .buf = buf, .idx = (unsigned)P.nit + 1, .frecency = -1 };
	if (put(text, tn, &i->text)) return -1;
	i->tlen = tn;
	if (file && put(file, fn, &i->file)) return -1;
	i->flen = file ? fn : 0;
	/* transform unique_file: a file listed once, as its first item (smart).
	   Only the buffers, which come first, can list a file fd lists too. */
	if (P.what == PICK_SMART && file && !buf)
		for (size_t k = 0; k < P.nit && P.it[k].buf; k++)
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
	P.npart = 0;
}

/* One line of a finder's output (source/files.lua, source/grep.lua). */
static void line(const char *s, size_t n)
{
	if (P.what != PICK_GREP) { add(s, n, s, n, 0, 0, 0); return; }
	/* rg -0: file NUL line:col:text.  The search names "." because rg
	   searches a piped stdin (ours is a pipe; snacks' is /dev/null), so
	   every file starts "./". */
	if (n > 2 && s[0] == '.' && s[1] == '/') s += 2, n -= 2;
	const char *z = memchr(s, 0, n);
	if (!z) return;
	size_t fn = (size_t)(z - s);
	const char *r = z + 1, *e = s + n;
	long ln = 0, col = 0;
	while (r < e && *r >= '0' && *r <= '9') ln = ln * 10 + (*r++ - '0');
	if (r == e || *r++ != ':') return;
	while (r < e && *r >= '0' && *r <= '9') col = col * 10 + (*r++ - '0');
	if (r == e || *r != ':') return;
	/* the text is "file:line:col:text" */
	char t[4096];
	size_t tn = (size_t)snprintf(t, sizeof t, "%.*s:%.*s", (int)fn, s, (int)(e - z - 1), z + 1);
	if (tn >= sizeof t) tn = sizeof t - 1;
	add(t, tn, s, fn, ln, col - 1, 0);
}

static void on_out(void *ctx, int stream, const char *b, size_t n)
{
	if ((int)(long)ctx != P.gen || !P.open) return;
	if (stream == 0) { P.proc = -1; P.done = 1; tools_dirty = 1; return; }
	if (stream != 1) return;
	size_t before = P.nshown;
	for (size_t i = 0; i < n; i++) {
		if (b[i] != '\n') {
			if (P.npart < sizeof P.part) P.part[P.npart++] = b[i];
			continue;
		}
		line(P.part, P.npart);
		P.npart = 0;
		if (P.what == PICK_GREP && P.nit >= LIMIT_LIVE) { finder_stop(); P.done = 1; break; }
	}
	if (P.nshown != before) {
		if (sorting()) qsort(P.shown, P.nshown, sizeof *P.shown, cmp);
		preview();
		tools_dirty = 1;
	}
}

static int spawn(char *const argv[])
{
	P.done = 0;
	P.proc = proc_spawn(argv, P.cwd, on_out, (void *)(long)P.gen);
	return P.proc;
}

/* source/files.lua: fd, else rg */
static void files(void)
{
	static char *fd[] = { "fd", "--type", "f", "--type", "l", "--color", "never", "-E", ".git", NULL };
	static char *rg[] = { "rg", "--files", "--no-messages", "--color", "never", "-g", "!.git", NULL };
	if (spawn(fd) < 0) spawn(rg);
}

/* source/grep.lua, live: the prompt is rg's pattern ("pattern -- args"
   splits off rg arguments: not kept) */
static void grep(void)
{
	char pat[PROMPT + 1];
	memcpy(pat, P.prompt, P.plen);
	pat[P.plen] = 0;
	finder_stop();
	P.nit = P.nshown = 0;
	P.an = 0;
	P.sel = P.top = 0;
	if (!P.plen) { P.done = 1; return; }     /* need_search */
	char *argv[] = { "rg", "--color=never", "--no-heading", "--with-filename", "--line-number",
		"--column", "--smart-case", "--max-columns=500", "--max-columns-preview",
		"--glob=!.bare", "--glob=!.git", "-0", "--no-hidden", "--", pat, ".", NULL };
	spawn(argv);
}

static const char *path_rel(const char *f, size_t *n)
{
	if (*n > P.cwdlen && !memcmp(f, P.cwd, P.cwdlen) && f[P.cwdlen] == '/') { *n -= P.cwdlen + 1; return f + P.cwdlen + 1; }
	return f;
}

/* source/buffers.lua: listed buffers, last used first */
static void buffers(Editor *e)
{
	for (Buf *b = e->bufs; b; b = b->next) {
		char t[3 * PATH_MAX], name[2 * PATH_MAX];
		/* Neovim's buffer name is the full path */
		if (!b->path) strcpy(name, "[Scratch]");
		else snprintf(name, sizeof name, "%s%s%s", b->path[0] == '/' ? "" : P.cwd, b->path[0] == '/' ? "" : "/", b->path);
		/* util.text(item, { "buf", "name", "filetype", "buftype" }), with no
		   filetype known here */
		int n = snprintf(t, sizeof t, "%d %s  ", b->nr, b->path ? name : "");
		add(t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1), name, strlen(name), 0, 0, b->nr);
	}
}

int tools_pick(Editor *e, int what)
{
	if (what != PICK_SMART && what != PICK_GREP && what != PICK_BUFFERS) {
		core_msg(e, "picker not built yet");
		return -1;
	}
	finder_stop();
	P.e = e;
	P.open = 1; P.what = what; P.insert = 1;
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
			int n = snprintf(f, sizeof f, "%s%s%s", b->path[0] == '/' ? "" : P.cwd, b->path[0] == '/' ? "" : "/", b->path);
			if (!stat(f, &st)) fr_visit(f, (size_t)n);
		}
	}
	if (what == PICK_GREP) return 0;
	buffers(e);
	if (what == PICK_SMART) files();
	preview();
	tools_dirty = 1;
	return 0;
}

static void close_picker(void)
{
	finder_stop();
	P.open = 0;
	tools_dirty = 1;
}

/* The prompt changed: match again, or for grep run rg again. */
static void changed(void)
{
	if (P.what == PICK_GREP) { grep(); return; }
	match_init(&P.m, P.prompt, P.plen);
	rematch();
}

/* actions.lua's jump: open the item's file (or buffer) at its position */
static void confirm(Editor *e)
{
	if (!P.nshown) { close_picker(); return; }
	Item it = P.it[P.shown[P.sel]];
	char f[PATH_MAX];
	snprintf(f, sizeof f, "%.*s", (int)it.flen, P.a + it.file);
	long line = it.line, col = it.col;
	/* a file:line:col typed into the prompt (matcher.file) */
	if (!line && P.m.line) { line = P.m.line; col = P.m.col; }
	close_picker();
	if (it.buf ? core_buf_goto(e, it.buf) : core_edit_file(e, f, 0)) return;
	/* BufWinEnter's visit (frecency.lua) */
	char abs[2 * PATH_MAX];
	int an = snprintf(abs, sizeof abs, "%s%s%s", f[0] == '/' ? "" : P.cwd, f[0] == '/' ? "" : "/", f);
	fr_visit(abs, (size_t)an);
	if (line) {
		size_t l = (size_t)line - 1, n = core_lines(e);
		e->win->line = l < n ? l : n - 1;
		size_t len = core_line_len(e, e->win->line);
		e->win->col = (size_t)col < len ? (size_t)col : len;
		core_clamp(e);
		core_set_want(e);
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
	size_t n = k < 0x80 ? 1 : k < 0x800 ? 2 : k < 0x10000 ? 3 : 4;
	if (n == 1) u[0] = (char)k;
	else {
		int lead = n == 2 ? 0xC0 : n == 3 ? 0xE0 : 0xF0;
		for (size_t i = n; i-- > 1; k /= 64) u[i] = (char)(0x80 + k % 64);
		u[0] = (char)(lead + k);
	}
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
	if (!P.open) return 0;
	key(e, k);
	if (P.open) preview();
	return 1;
}

static int key(Editor *e, int k)
{
	static int g;                      /* a g typed in normal mode: gg */
	tools_dirty = 1;
	int h = list_height(e);
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
	if (g) { g = 0; if (k == 'g') P.sel = 0; return 1; }
	switch (k) {
	case KEY_ESC: case 'q': close_picker(); break;
	case 'j': move(1); break;
	case 'k': move(-1); break;
	case 'G': if (P.nshown) P.sel = P.nshown - 1; break;
	case 'g': g = 1; break;
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

static void fill(Grid *g, Box b)
{
	for (int r = b.y; r < b.y + b.h; r++)
		for (int c = b.x; c < b.x + b.w; c++) grid_put(g, r, c, (const unsigned char *)" ", 1, 1, ATTR_NONE);
}

static void hline(Grid *g, int r, int x, int w, const char *l, const char *m, const char *rt)
{
	grid_put(g, r, x, (const unsigned char *)l, strlen(l), 1, ATTR_NONE);
	for (int c = x + 1; c < x + w - 1; c++) grid_put(g, r, c, (const unsigned char *)m, strlen(m), 1, ATTR_NONE);
	grid_put(g, r, x + w - 1, (const unsigned char *)rt, strlen(rt), 1, ATTR_NONE);
}

/* rounded border with a centred title */
static void border(Grid *g, Box b, const char *title)
{
	hline(g, b.y, b.x, b.w, "╭", "─", "╮");
	for (int r = b.y + 1; r < b.y + b.h - 1; r++) {
		grid_put(g, r, b.x, (const unsigned char *)"│", 3, 1, ATTR_NONE);
		grid_put(g, r, b.x + b.w - 1, (const unsigned char *)"│", 3, 1, ATTR_NONE);
	}
	hline(g, b.y + b.h - 1, b.x, b.w, "╰", "─", "╯");
	if (title) grid_puts(g, b.y, b.x + (b.w - (int)strlen(title)) / 2, title, ATTR_NONE);
}

/* UTF-8 text into a row from column x, at most to column end; returns the
   next column */
static int text(Grid *g, int r, int x, int end, const char *s, size_t n, int attr)
{
	for (size_t i = 0; i < n && x < end;) {
		size_t k = uni_len((const unsigned char *)s, n, i);
		unsigned cp = uni_cp((const unsigned char *)s, n, i);
		int w = cp < 0x20 || cp == 0x7f ? 1 : uni_width(cp);
		if (x + w > end) break;
		if (cp < 0x20 || cp == 0x7f) grid_put(g, r, x, (const unsigned char *)" ", 1, 1, attr);
		else grid_put(g, r, x, (const unsigned char *)s + i, k, w, attr);
		x += w; i += k;
	}
	return x;
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
	if (want < 0) return;
	Item *it = &P.it[want];
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

void tools_draw(Editor *e, Grid *g)
{
	if (!P.open) return;
	static const char *title[] = { " Smart ", " Grep ", " Buffers " };
	Box o, l, p;
	int wide, h = list_height(e);
	layout(e, &o, &l, &p, &wide);
	fill(g, o);
	border(g, l, title[P.what]);
	border(g, p, NULL);
	/* the prompt, its line, and the count at its right (input.lua) */
	int x = grid_puts(g, l.y + 1, l.x + 2, "> ", ATTR_NONE);
	int px = text(g, l.y + 1, x, l.x + l.w - 1, P.prompt, P.pcur, ATTR_NONE);
	text(g, l.y + 1, px, l.x + l.w - 1, P.prompt + P.pcur, P.plen - P.pcur, ATTR_NONE);
	char cnt[48];
	snprintf(cnt, sizeof cnt, "%zu/%zu ", P.nshown, P.nit);
	grid_puts(g, l.y + 1, l.x + l.w - 1 - (int)strlen(cnt), cnt, ATTR_DIM);
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
			c = grid_puts(g, row, c + 1, nr, a ? a : ATTR_DIM);
		}
		/* format "file": the directory dimmed, then the name */
		size_t fn = it->flen ? it->flen : it->tlen;
		const char *f = path_rel(P.a + (it->flen ? it->file : it->text), &fn);
		size_t d = fn;
		while (d && f[d - 1] != '/') d--;
		c = text(g, row, c + 1, end, f, d, a ? a : ATTR_DIM);
		c = text(g, row, c, end, f + d, fn - d, a);
		if (it->line) {
			/* grep: :line:col: and the line */
			const char *t = P.a + it->text + it->flen + 1;
			size_t tn = it->tlen - it->flen - 1;
			c = text(g, row, c, end, ":", 1, a);
			text(g, row, c, end, t, tn, a);
		}
	}
	/* the preview */
	for (int r = 0; r < P.pvn && r < p.h - 2; r++)
		text(g, p.y + 1 + r, p.x + 1, p.x + p.w - 1, P.pv[r], strlen(P.pv[r]), ATTR_NONE);
	g->crow = l.y + 1;
	g->ccol = px;
}

