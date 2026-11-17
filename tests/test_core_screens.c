/* Screens against Neovim (package 4): replays the cases that
   tests/test_core_screens.py recorded from nvim --headless and compares
   every screen row above the command line.  Each token becomes the core
   call the keys make for it (package 5 binds the keys); "k:" tokens go
   through keys_feed.  usage: test_core_screens [FILE] (default
   tests/test_core_screens.txt); with -v, prints each differing screen. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "../src/core/core_int.h"

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

/* Neovim's key notation in a "k:" token: <Esc> <CR> <C-r>, the rest literal */
static void feed(Editor *e, const char *s)
{
	int keys[512], n = 0;
	while (*s && n < 512) {
		if (!strncmp(s, "<Esc>", 5)) { keys[n++] = KEY_ESC; s += 5; }
		else if (!strncmp(s, "<CR>", 4)) { keys[n++] = KEY_ENTER; s += 4; }
		else if (!strncmp(s, "<C-r>", 5)) { keys[n++] = KEY_CTRL_R; s += 5; }
		else keys[n++] = (unsigned char)*s++;
	}
	for (int i = 0; i < n; i++) keys_feed(e, keys[i], 0);   /* no typeahead: see the .py */
}

static int status_row(const Editor *e, int r)
{
	for (const Win *w = e->wins; w; w = w->next) if (r == w->y + w->h) return 1;
	return 0;
}

static const char *row_text(const Grid *g, int r)
{
	static char got[1 << 12];
	size_t k = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		memcpy(got + k, x->ch, x->len);
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
	if (!strncmp(t, "e:", 2)) { core_edit_file(e, t + 2, 0); goto upd; }
	if (!strncmp(t, "b:", 2)) { core_buf_goto(e, atoi(t + 2)); goto upd; }
	if (!strncmp(t, "size:", 5)) {
		int r, c;
		CHECK(sscanf(t + 5, "%d:%d", &r, &c) == 2 && !grid_resize(g, r, c));
		return;
	}
	if (t[0] == 'w' && strchr(t, ':')) {          /* CTRL-W with a count */
		const char *p = t + 1;
		long n = count_of(&p);
		int c = p[1];
		switch (c) {
		case 's': case 'v': core_split(e, c == 'v', (int)n); break;
		case 'c': case 'q': if (!one) core_close(e, 0); break;
		case 'o': core_only(e); break;
		case 'w': case 'W': case 'h': case 'j': case 'k': case 'l': case 't': case 'b': case 'p':
			core_win_goto(e, c, n); break;
		case '=': core_equal(e); break;
		case '+': case '-': core_resize(e, 0, c == '+' ? (n ? n : 1) : -(n ? n : 1), 0); break;
		case '>': case '<': core_resize(e, 1, c == '>' ? (n ? n : 1) : -(n ? n : 1), 0); break;
		case '_': core_resize(e, 0, n, 1); break;
		case '|': core_resize(e, 1, n, 1); break;
		default: core_win_move(e, c, n);
		}
		goto upd;
	}
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
	const char *p = t;
	long n = count_of(&p);
	if (!strcmp(p, "C-e")) core_scroll(e, n ? n : 1);
	else if (!strcmp(p, "C-y")) core_scroll(e, -(n ? n : 1));
	else if (!strcmp(p, "C-d") || !strcmp(p, "C-u")) core_scroll_half(e, p[2] == 'd' ? 1 : -1, n);
	else if (!strcmp(p, "C-f") || !strcmp(p, "C-b")) core_scroll_page(e, p[2] == 'f' ? 1 : -1, n);
	else if (p[0] == 'z') core_scroll_cursor(e, p[1] == 't' ? 't' : p[1] == 'z' ? 'z' : 'b');
	else if (!strcmp(p, "H") || !strcmp(p, "M") || !strcmp(p, "L")) {
		/* nv_scroll(), then beginline(BL_SOL | BL_FIX): nostartofline keeps curswant */
		w->line = core_screen_line(e, p[0], n ? n : 1);
		w->col = core_col_at(e, w->line, w->want, 0);
	} else if (!strcmp(p, "gj") || !strcmp(p, "gk")) core_screen_move(e, p[1] == 'j' ? (n ? n : 1) : -(n ? n : 1));
	else { fprintf(stderr, "unknown token %s\n", t); exit(2); }
upd:
	core_update(e);
}

int main(int argc, char **argv)
{
	const char *path = "tests/test_core_screens.txt";
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	for (int i = 0; i < 30; i++) {
		pum_items[i] = PUM1[i % 10];
		snprintf(pum_words[i], sizeof pum_words[i], "%s%s", PUM1[i % 10].word, (const char *[]){ "", "2", "3" }[i / 10]);
		pum_items[i].word = pum_words[i];
	}
	FILE *f = fopen(path, "r");
	CHECK(f);
	char cwd[4096], dir[] = "/tmp/core_screensXXXXXX";
	CHECK(getcwd(cwd, sizeof cwd) && mkdtemp(dir));
	static char *tokens[64], *screens[65][121], *marks[65];
	int srow = 0;
	int ntok = 0, nscr = 0, nfile = 0, rows = 0, cols = 0, ncase = 0, bad = 0, total = 0, badcases = 0;
	while (fgets(line, sizeof line, f)) {
		size_t ll = strcspn(line, "\n");
		CHECK(line[ll] == '\n');                  /* a longer line would be read as two */
		while (ll > 2 && line[ll - 1] == ' ') ll--;
		line[ll] = 0;
		if (line[0] == 'C') { CHECK(sscanf(line, "C %d %d", &rows, &cols) == 2); ntok = nscr = nfile = 0; }
		else if (line[0] == 'F') {
			char name[64];
			snprintf(name, sizeof name, "%s/f%d", dir, nfile++);
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
			CHECK(!chdir(dir) && !core_open(&e, "f0") && !grid_resize(&g, rows, cols));
			in_pum = 0;
			for (int s = 0; s < nscr; s++) {
				if (s) token(&e, &g, tokens[s - 1]);
				core_draw(&e, &g);
				total++;
				/* rows above the command line: Neovim's headless command line
				   shows typed commands and keeps no messages */
				int below = 0, r;
				for (Win *w = e.wins; w; w = w->next) if (w->y + w->h + 1 > below) below = w->y + w->h + 1;
				for (r = 0; r < below && screens[s][r]; r++)
					if (strcmp(row_text(&g, r), screens[s][r]) && !(in_pum && status_row(&e, r))) break;
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
				snprintf(name, sizeof name, "%s/f%d", dir, i);
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
	rmdir(dir);
	if (bad) { printf("test_core_screens: %d of %d screens differ from Neovim (%d of %d cases)\n", bad, total, badcases, ncase); return 1; }
	printf("test_core_screens: %d screens of %d cases match Neovim\n", total, ncase);
	return 0;
}
