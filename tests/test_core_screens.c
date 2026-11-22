/* Screens against Neovim (package 4): replays the cases that
   tests/test_core_screens.py recorded from nvim --headless and compares
   every screen row above the command line.  Each token becomes the core
   call the keys make for it (package 5 binds the keys); "k:" tokens go
   through keys_feed.  usage: test_core_screens [FILE] (default
   tests/test_core_screens.txt, then tests/test_core_virt.txt); with -v,
   prints each differing screen.
   A case marked V (the .py's --virt) has Org files whose image links
   [[file:pK.png]] show K filler rows: a 1 x K pixel PNG in 1 x 1 pixel
   cells, an image cell read as '#'. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "uni.h"
#include "../src/core/core_int.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static char line[1 << 20];   /* a file is one line of hex: up to 512 KiB */

/* the popup's items: PUM in the .py */
static const PumItem PUM1[] = {
	{ "foo", "v", "" }, { "foobar", "f", "module.c" }, { "é", "", "λx menu" },
	{ "longerword_completion", "m", "" }, { "x", "k", "a menu that is longer than the rest" },
	{ "ა_b", "", "" }, { "bar_1", "v", "m" }, { "λx", "f", "" },
	{ "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy", "", "" }, { "z", "v", "zz" },
};
static PumItem pum_items[3 * 10];
static char pum_words[3 * 10][48];
static int verbose;
/* Neovim redraws no status line while the popup is up, so the status lines
   of a screen read then show the state before the completion: left out */
static int in_pum;

/* the keys of a "k:" token (test_key: package 4's, agreed by thread 0) */
static void feed(Editor *e, const char *s)
{
	while (*s) keys_feed(e, test_key(&s), 0);   /* no typeahead: see the .py */
}

/* Row r is a status line whose cursor agrees with Neovim's: the core draws
   lualine's (test_core_status), the trace holds Neovim's default one, so
   only its ruler "%l,%c%V" is compared, where the window's part of the row
   shows it whole: the line, the byte column and the screen column ("0-1"
   on an empty line, line 0 in an empty buffer) */
static int status_row(Editor *e, int r, const char *row)
{
	for (Win *w = e->wins; w; w = w->next) {
		if (r != w->y + w->h) continue;
		if (in_pum) return 1;
		/* the window's cells of the row, as one string between blanks */
		const unsigned char *u = (const unsigned char *)row;
		size_t n = strlen(row), i = 0, a;
		char part[1024] = " ", want[64];
		for (int c = 0; i < n && c < w->x; i += uni_glen(u, n, i)) c += uni_width(u, n, i);
		for (a = i; i < n && (int)(i - a) < (int)sizeof part - 3; i += uni_glen(u, n, i)) {
			int c = 0;
			for (size_t k = a; k < i; k += uni_glen(u, n, k)) c += uni_width(u, n, k);
			if (c >= w->w) break;
		}
		memcpy(part + 1, row + a, i - a);
		strcpy(part + 1 + (i - a), " ");
		size_t len = text_line_start(w->buf->text, w->line + 1) - text_line_start(w->buf->text, w->line) - 1,
		       l = w->buf->flags & CORE_EMPTY ? 0 : w->line + 1, col = len ? (w->col < len ? w->col : len) + 1 : 0,
		       v = win_vcol(e, w, NULL, NULL) + 1;
		if (!col) snprintf(want, sizeof want, " %zu,0-1 ", l);
		else if (v != col) snprintf(want, sizeof want, " %zu,%zu-%zu ", l, col, v);
		else snprintf(want, sizeof want, " %zu,%zu ", l, col);
		/* Neovim's default status line: the name, "[+]" while modified
		   (unless the name was cut with '<'), the ruler */
		int plus = !!strstr(part, "[+]");
		if (plus != core_modified(w->buf) && (plus || !strchr(part, '<'))) return 0;
		if (strstr(part, want)) return 1;
		/* a ruler cut by the window's width ('<' before it) is not compared */
		for (const char *p = part; *p; p++) {
			unsigned long x, y;
			char end;
			if (p[0] == ' ' && sscanf(p + 1, "%lu,%lu%c", &x, &y, &end) == 3 && (end == ' ' || end == '-')) return 0;
		}
		return 1;
	}
	return 0;
}

static const char *row_text(const Grid *g, int r)
{
	static char got[1 << 12];
	size_t k = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		if (x->attr == ATTR_IMAGE) { got[k++] = '#'; continue; }
		memcpy(got + k, grid_text(x), x->len);   /* a long cluster is in the store */
		k += x->len;
	}
	while (k && got[k - 1] == ' ') k--;   /* the file keeps no trailing spaces */
	got[k] = 0;
	return got;
}

static long count_of(const char **t)
{
	long n = 0;
	while (**t >= '0' && **t <= '9') n = n * 10 + *(*t)++ - '0';
	return n;
}

static void token(Editor *e, Grid *g, const char *t)
{
	Win *w = e->win;
	int one = !e->wins->next;
	if (!strcmp(t, "nop")) return;         /* a token the recorder left out: see the .py */
	if (!strncmp(t, "pum:", 4)) {
		/* A, then what complete() and CTRL-N do: the selected word (none
		   after the last) typed after the line's end, the popup shown */
		int a, n, s;
		CHECK(sscanf(t + 4, "%d:%d:%d", &a, &n, &s) == 3);
		keys_feed(e, 'A', 0);
		size_t col = w->col, at = core_line_start(e, w->line) + col, len = 0;
		for (int k = 0; k <= s; k++) {          /* complete(), then each CTRL-N */
			int sel = k % (n + 1) == n ? -1 : k % (n + 1);
			const char *word = sel < 0 ? "" : pum_items[a + sel].word;
			CHECK(!core_edit(e, at, len, word, strlen(word)));
			len = strlen(word);
			w->col = col + len;
			/* 'completeopt' "menu,popup", without menuone: no popup for one
			   match, and Neovim redraws after each CTRL-N (second review
			   of PR 20, seed 7 case 368) */
			core_pum(e, pum_items + a, n > 1 ? n : 0, sel, col);
			if (n == 1) core_update(e);
		}
		in_pum = 1;
		return;
	}
	if (!strncmp(t, "k:", 2)) { feed(e, t + 2); return; }
	if (!strncmp(t, "m:", 2)) { e->buf->mark[t[2] - 'a'] = (Mark){ w->line, w->col, 1 }; return; }
	if (!strncmp(t, "size:", 5)) {
		int r, c;
		CHECK(sscanf(t + 5, "%d:%d", &r, &c) == 2 && !grid_resize(g, r, c));
		return;
	}
	/* CTRL-W with a count, and the ':' of the ex commands (":sp<CR>"), go
	   through the keys as the recorder types them, so normal_cmd() settles
	   the wanted column of the window they leave (PR 62's review); the keys
	   have no such ex command (nor CTRL-^), so the core then runs it */
	if (t[0] == 'w' && strchr(t, ':')) {
		const char *p = t + 1;
		count_of(&p);
		if (one && (p[1] == 'c' || p[1] == 'q')) goto upd;
		for (const char *c = t + 1; c < p; c++) keys_feed(e, *c, 0);
		keys_feed(e, 23, 0);
		keys_feed(e, p[1], 0);
		goto upd;
	}
	static const char *const ex[] = { "wrap", "nowrap", "sp", "vs", "new", "vnew", "close", "q", "only",
	                                  "bn", "bp", "alt", "enew", "bd" };
	int x = t[1] == ':' && (t[0] == 'e' || t[0] == 'b');
	for (size_t i = 0; i < sizeof ex / sizeof *ex; i++) x |= !strcmp(t, ex[i]);
	if (x) { keys_feed(e, ':', 0); keys_feed(e, KEY_ESC, 0); }
	if (!strncmp(t, "e:", 2)) { core_edit_file(e, t + 2, 0); goto upd; }
	if (!strncmp(t, "b:", 2)) { core_buf_goto(e, atoi(t + 2)); goto upd; }
	if (!strcmp(t, "wrap") || !strcmp(t, "nowrap")) { core_set_wrap(e, t[0] == 'w'); goto upd; }
	if (!strcmp(t, "sp") || !strcmp(t, "vs")) { core_split(e, t[0] == 'v', 0); goto upd; }
	if (!strcmp(t, "new") || !strcmp(t, "vnew")) {
		core_new(e, t[0] == 'v');
		goto upd;
	}
	if (!strcmp(t, "close") || !strcmp(t, "q")) { if (!one) core_close(e, 0); goto upd; }
	if (!strcmp(t, "only")) { core_only(e); goto upd; }
	if (!strcmp(t, "bn") || !strcmp(t, "bp")) { core_buf_next(e, t[1] == 'n' ? 1 : -1); goto upd; }
	if (!strcmp(t, "alt")) {
		core_buf_alt(e);
		goto upd;
	}
	if (!strcmp(t, "enew")) { core_edit_file(e, "", 0); goto upd; }
	if (!strcmp(t, "bd")) { core_buf_delete(e, 0, 0); goto upd; }
	/* the scrolling keys go through keys_feed as typed, count first, so
	   keys update the wanted column first as Neovim's lazy curswant does
	   (calling the core directly skipped that: PR 26) */
	const char *p = t;
	count_of(&p);
	int k = !strcmp(p, "C-e") ? 5 : !strcmp(p, "C-y") ? 25 : !strcmp(p, "C-d") ? 4 : !strcmp(p, "C-u") ? 21
	      : !strcmp(p, "C-f") ? 6 : !strcmp(p, "C-b") ? 2 : 0;
	const char *ks = k ? NULL : !strcmp(p, "zt") ? "zt" : !strcmp(p, "zz") ? "zz" : !strcmp(p, "zb") ? "zb"
	      : !strcmp(p, "H") ? "H" : !strcmp(p, "M") ? "M" : !strcmp(p, "L") ? "L"
	      : !strcmp(p, "gj") ? "gj" : !strcmp(p, "gk") ? "gk" : NULL;
	if (!k && !ks) { fprintf(stderr, "unknown token %s\n", t); exit(2); }
	for (const char *c = t; c < p; c++) keys_feed(e, *c, 0);
	if (k) keys_feed(e, k, 0);
	else for (; *ks; ks++) keys_feed(e, *ks, 0);
upd:
	core_update(e);
}

static int replay(const char *path)
{
	FILE *f = fopen(path, "r");
	CHECK(f);
	char cwd[4096], dir[] = "/tmp/core_screensXXXXXX";
	CHECK(getcwd(cwd, sizeof cwd) && mkdtemp(dir));
	static unsigned char px[12 * 4];
	memset(px, 255, sizeof px);
	for (int k = 1; k <= 12; k++) {
		char name[64];
		snprintf(name, sizeof name, "%s/p%d.png", dir, k);
		CHECK(stbi_write_png(name, 1, k, 4, px, 4));
	}
	int virt = 0;
	static char *tokens[64], *screens[65][121], *marks[65];
	int srow = 0;
	int ntok = 0, nscr = 0, nfile = 0, rows = 0, cols = 0, ncase = 0, bad = 0, total = 0, badcases = 0;
	while (fgets(line, sizeof line, f)) {
		size_t ll = strcspn(line, "\n");
		CHECK(line[ll] == '\n');                  /* a longer line would be read as two */
		while (ll > 2 && line[ll - 1] == ' ') ll--;
		line[ll] = 0;
		if (line[0] == 'C') { CHECK(sscanf(line, "C %d %d", &rows, &cols) == 2); ntok = nscr = nfile = virt = 0; }
		else if (line[0] == 'V') virt = 1;
		else if (line[0] == 'F') {
			char name[64];
			snprintf(name, sizeof name, "%s/f%d%s", dir, nfile++, virt ? ".org" : "");
			FILE *o = fopen(name, "w");
			CHECK(o);
			for (const char *h = line + 2; h[0] && h[1]; h += 2) {
				unsigned b;
				CHECK(sscanf(h, "%2x", &b) == 1);
				fputc((int)b, o);
			}
			fclose(o);
		} else if (line[0] == 'T') { CHECK(ntok < 64); tokens[ntok++] = strdup(line + 2); }
		else if (line[0] == 'P') { CHECK(nscr < 65); nscr++; srow = 0; }
		else if (line[0] == 'S') { CHECK(srow < 120); screens[nscr - 1][srow++] = strdup(line + 2); }
		else if (line[0] == 'M') marks[nscr - 1] = strdup(line + 2);
		else if (line[0] == 'R') {                 /* the row as on the screen before */
			CHECK(srow < 120 && nscr > 1 && screens[nscr - 2][srow]);
			screens[nscr - 1][srow] = strdup(screens[nscr - 2][srow]);
			srow++;
		}
		else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			int differ = 0;
			core_pixw = core_pixh = virt;
			CHECK(!chdir(dir) && !core_open(&e, virt ? "f0.org" : "f0") && !grid_resize(&g, rows, cols));
			in_pum = 0;
			for (int s = 0; s < nscr; s++) {
				if (s) token(&e, &g, tokens[s - 1]);
				core_draw(&e, &g);
				total++;
				/* rows above the command line, the status lines by their
				   rulers: Neovim's headless command line shows typed commands
				   and keeps no messages */
				int below = 0, r;
				for (Win *w = e.wins; w; w = w->next) if (w->y + w->h + 1 > below) below = w->y + w->h + 1;
				for (r = 0; r < below && screens[s][r]; r++)
					if (strcmp(row_text(&g, r), screens[s][r]) && !status_row(&e, r, screens[s][r])) break;
				char mk[3][48], got[160];
				for (int i = 0; i < 3; i++) {
					Mark m = e.buf->mark[i];
					if (m.set) snprintf(mk[i], sizeof mk[i], "%zu:%zu", m.line, m.col);
					else strcpy(mk[i], "-");
				}
				snprintf(got, sizeof got, "%s %s %s", mk[0], mk[1], mk[2]);
				if (marks[s] && strcmp(got, marks[s])) {
					bad++;
					if (!differ++ || verbose)
						fprintf(stderr, "case %d after %s: marks\n  nvim [%s]\n  core [%s]\n", ncase,
						        s ? tokens[s - 1] : "start", marks[s], got);
				} else if (r < below && screens[s][r]) {
					bad++;
					if (!differ++ || verbose)
						fprintf(stderr, "case %d (%dx%d) after %s, row %d:\n  nvim [%s]\n  core [%s]\n", ncase,
						        rows, cols, s ? tokens[s - 1] : "start", r, screens[s][r], row_text(&g, r));
					if (verbose)
						for (int k = 0; k < g.rows && screens[s][k]; k++)
							fprintf(stderr, "  %2d nvim [%s]\n     core [%s]\n", k, screens[s][k], row_text(&g, k));
				}
			}
			badcases += differ > 0;
			core_free(&e);
			grid_free(&g);
			CHECK(!chdir(cwd));
			for (int i = 0; i < nfile; i++) {
				char name[64];
				snprintf(name, sizeof name, "%s/f%d%s", dir, i, virt ? ".org" : "");
				unlink(name);
			}
			for (int i = 0; i < ntok; i++) free(tokens[i]);
			for (int s = 0; s < nscr; s++) {
				for (int r = 0; screens[s][r]; r++) free(screens[s][r]);
				free(marks[s]);
				marks[s] = NULL;
			}
			memset(screens, 0, sizeof screens);
			ncase++;
		}
	}
	fclose(f);
	for (int k = 1; k <= 12; k++) {
		char name[64];
		snprintf(name, sizeof name, "%s/p%d.png", dir, k);
		unlink(name);
	}
	rmdir(dir);
	if (bad) { printf("%s: %d of %d screens differ from Neovim (%d of %d cases)\n", path, bad, total, badcases, ncase); return 1; }
	printf("%s: %d screens of %d cases match Neovim\n", path, total, ncase);
	return 0;
}

int main(int argc, char **argv)
{
	const char *path = NULL;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	for (int i = 0; i < 30; i++) {
		pum_items[i] = PUM1[i % 10];
		snprintf(pum_words[i], sizeof pum_words[i], "%s%s", PUM1[i % 10].word, (const char *[]){ "", "2", "3" }[i / 10]);
		pum_items[i].word = pum_words[i];
	}
	/* the traces without and with images (test_core_screens.py --virt) */
	return path ? replay(path) : replay("tests/test_core_screens.txt") | replay("tests/test_core_virt.txt");
}
