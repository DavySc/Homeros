/* render-markdown (headings, bullets, checkboxes, fenced code) against
   Neovim: replays the cases that tests/test_core_markdown.py recorded from
   Neovim with render-markdown (tests/test_core_markdown.txt: 1000 random,
   then tests/test_core_markdown_cases.py's 44: sideways scroll, skipcol,
   fillers, Visual mode) and compares every row's text, the colours the .py
   names, and the cursor.  usage: test_core_markdown [FILE] [-v] */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "syntax.h"
#include "../src/core/core_int.h"

static char line[4096];

/* a letter per colour the .py names */
static char letter(const unsigned char *c, const char *const *hex, const char *set)
{
	char h[8];
	snprintf(h, sizeof h, "%02x%02x%02x", c[0], c[1], c[2]);
	for (int i = 0; set[i]; i++) if (!strcmp(h, hex[i])) return set[i];
	return '.';
}
static const char *const BG[] = { "32283a", "332c36", "33313a", "2b3139", "262e40", "2c2d42", "2a2b3c", "181825" };
static const char *const FG[] = { "f38ba8", "fab387", "f9e2af", "a6e3a1", "74c7ec", "b4befe", "89dceb", "7f849c", "181825" };

/* row r of g as the .py writes it: 'S' text, 'B' backgrounds, 'F'
   foregrounds (none on a blank) */
static const char *row(const Grid *g, int r, char what)
{
	static char b[1024];
	size_t n = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		/* a right half shows its left half's colours */
		if (!x->len && c) x--;
		const Face *bg, *ul, *f = grid_face(x, &bg, &ul);
		if (what == 'S') { if (x == &g->c[r * g->cols + c]) { memcpy(b + n, grid_text(x), x->len); n += x->len; } }
		else if (what == 'B') b[n++] = bg->style & FACE_BG ? letter(bg->bg, BG, "RPYGSLCM") : '.';
		else b[n++] = f->style & FACE_FG && !(x->len == 1 && x->ch[0] == ' ') ? letter(f->fg, FG, "rpygslbuc") : '.';
	}
	b[n] = 0;
	return b;
}

static int status_row(const Editor *e, int r)
{
	for (const Win *w = e->wins; w; w = w->next) if (r == w->y + w->h) return 1;
	return 0;
}

/* a command as the .py's nvkeys(), then the frames and the parse main.c
   makes before the next one: until nothing changes, for the injected inline
   grammar of the lines in view */
static void command(Editor *e, Grid *g, const char *t)
{
	if (!strncmp(t, "k:", 2)) for (const char *s = t + 2; *s; s++) keys_feed(e, (unsigned char)*s, 0);
	else if (!strcmp(t, "wrap")) core_set_wrap(e, 1);
	/* as CTRL-W s, v and w typed: a window command through the keys first
	   settles the wanted column of the window it leaves (normal_cmd()) */
	else if (!strcmp(t, "sp") || !strcmp(t, "vs") || !strncmp(t, "w:", 2)) {
		keys_feed(e, 23, 0);
		keys_feed(e, t[0] == 'w' ? t[2] : t[0] == 'v' ? 'v' : 's', 0);
	}
	else if (!strncmp(t, "e:", 2)) CHECK(!core_edit_file(e, t + 2, 0));
	/* 'v': the area from the cursor */
	else if (!strcmp(t, "visual")) e->ks.visual = 'v', e->ks.vline = e->win->line, e->ks.vcol = e->win->col;
	else if (!strcmp(t, "redraw")) {}   /* CTRL-L: the core draws whole frames */
	else CHECK(!"command");
	do core_draw(e, g);
	while (syntax_update(e->buf->syn, e->buf->text, NULL));
}

/* Cases listed as known, 22 of 1044.  Drawing's: an ordered number wider
   than its marker, which Neovim puts inline (1032); a tab before a
   heading's marker (1041); an info string past the 256 bytes draw_md
   reads, in a window wider than they are (1042).  The core's (package 4,
   reported): the window's top after a split (9 ... 961), and with wrap
   after a jump near a line taller than scrolloff allows (110 ... 979);
   after v on a Tab without a move, Neovim keeps the cursor on the Tab's
   last cell (the w_virtcol n_start_visual_mode() leaves valid), the core
   moves it to the first (654, 738).  The syntax package's: an inline colour
   Neovim does not give (272). */
static int known(int n)
{
	static const int k[] = { 9, 10, 11, 73, 138, 506, 549, 683, 693, 731, 882, 961,
	                         110, 330, 854, 979, 654, 738, 272, 1032, 1041, 1042 };
	for (size_t i = 0; i < sizeof k / sizeof *k; i++) if (k[i] == n) return 1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *path = "tests/test_core_markdown.txt";
	int verbose = 0, ncase = 0, nknown = 0, bad = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	FILE *in = fopen(path, "r"), *md = NULL, *md2 = NULL;
	char dir[] = "/tmp/test_core_markdown.XXXXXX", cwd[4096];
	static char toks[16][64], want[64][3][1024];
	CHECK(in && mkdtemp(dir) && getcwd(cwd, sizeof cwd));
	int rows = 0, cols = 0, nrow = 0, ntok = 0, xr = 0, xc = 0;
	while (fgets(line, sizeof line, in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line + 2, "%d %d", &rows, &cols) == 2 && rows <= 64);
			CHECK(!chdir(dir) && (md = fopen("f.md", "w")) && (md2 = fopen("g.md", "w")) && !chdir(cwd));
			nrow = ntok = 0;
		} else if (line[0] == 'L' || line[0] == 'M') fprintf(line[0] == 'L' ? md : md2, "%s\n", line + 2);
		else if (line[0] == 'T') { CHECK(ntok < 16 && strlen(line + 2) < sizeof toks[0]); strcpy(toks[ntok++], line + 2); }
		else if (line[0] == 'X') CHECK(sscanf(line + 2, "%d %d", &xr, &xc) == 2);
		else if (line[0] == 'S' || line[0] == 'B' || line[0] == 'F') {
			int k = line[0] == 'S' ? 0 : line[0] == 'B' ? 1 : 2;
			CHECK(nrow < 64 && strlen(line + 2) < sizeof want[nrow][k]);
			strcpy(want[nrow][k], line + 2);
			if (k == 2) nrow++;
		} else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			fclose(md);
			fclose(md2);
			CHECK(!chdir(dir) && !core_open(&e, "f.md") && !grid_resize(&g, rows, cols));
			CHECK(e.buf->syn);
			/* a first frame tells the core its size, as main.c: before it
			   the window is Neovim's default 80 columns */
			command(&e, &g, "k:");
			for (int t = 0; t < ntok; t++) command(&e, &g, toks[t]);
			if (known(++ncase)) nknown++;
			int diff = !known(ncase) && e.mode != MODE_CMDLINE && (g.crow != xr || g.ccol != xc);
			if (diff && (!bad || verbose))
				fprintf(stderr, "case %d cursor: nvim %d,%d core %d,%d\n", ncase, xr, xc, g.crow, g.ccol);
			/* every row but the status lines (mini.statusline's, test_core_status) */
			for (int r = 0; r < nrow && !diff && !known(ncase); r++)
				for (int k = 0; k < 3 && !diff && !status_row(&e, r); k++)
					if (strcmp(row(&g, r, "SBF"[k]), want[r][k])) {
						diff = 1;
						if (!bad || verbose)
							fprintf(stderr, "case %d row %d %c:\n  nvim [%s]\n  core [%s]\n", ncase,
							        r, "SBF"[k], want[r][k], row(&g, r, "SBF"[k]));
					}
			if (diff && verbose) for (int r = 0; r < nrow; r++) fprintf(stderr, "  core S %s\n", row(&g, r, 'S'));
			bad += diff;
			core_free(&e);
			grid_free(&g);
			CHECK(!unlink("f.md") && !unlink("g.md") && !chdir(cwd));
		}
	}
	fclose(in);
	rmdir(dir);
	printf("test_core_markdown: %d render-markdown screens: %d differ (%d known, not compared)\n", ncase, bad, nknown);
	return bad != 0;
}
