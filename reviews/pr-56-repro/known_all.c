/* Review of PR 56, second pass: tests/test_core_markdown.c (ad620a8) with no case
   skipped as known and every differing row printed, to see what the 25 known cases hide. */
/* render-markdown's headings against Neovim: replays the cases that
   tests/test_core_markdown.py recorded from Neovim with render-markdown
   (tests/test_core_markdown.txt) and compares every row's text, background
   and heading colours, and the cursor.  usage: test_core_markdown [FILE] [-v] */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "syntax.h"
#include "../src/core/core_int.h"

static char line[4096];

/* one letter per heading background and per rainbow foreground, as the .py */
static char letter(const unsigned char *c, const char *const *hex, const char *set)
{
	char h[8];
	snprintf(h, sizeof h, "%02x%02x%02x", c[0], c[1], c[2]);
	for (int i = 0; i < 6; i++) if (!strcmp(h, hex[i])) return set[i];
	return '.';
}
static const char *const BG[] = { "32283a", "332c36", "33313a", "2b3139", "262e40", "2c2d42" };
static const char *const FG[] = { "f38ba8", "fab387", "f9e2af", "a6e3a1", "74c7ec", "b4befe" };

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
		const Face *bg, *f = grid_face(x, &bg);
		if (what == 'S') { if (x == &g->c[r * g->cols + c]) { memcpy(b + n, grid_text(x), x->len); n += x->len; } }
		else if (what == 'B') b[n++] = bg->style & FACE_BG ? letter(bg->bg, BG, "RPYGSL") : '.';
		else b[n++] = f->style & FACE_FG && !(x->len == 1 && x->ch[0] == ' ') ? letter(f->fg, FG, "rpygsl") : '.';
	}
	b[n] = 0;
	return b;
}

/* a command as the .py's nvkeys(), then the frames and the parse main.c
   makes before the next one: until nothing changes, for the injected inline
   grammar of the lines in view */
static void command(Editor *e, Grid *g, const char *t)
{
	if (!strncmp(t, "k:", 2)) for (const char *s = t + 2; *s; s++) keys_feed(e, (unsigned char)*s, 0);
	else if (!strcmp(t, "wrap")) core_set_wrap(e, 1);
	else if (!strcmp(t, "sp") || !strcmp(t, "vs")) CHECK(!core_split(e, t[0] == 'v', 0));
	else if (!strncmp(t, "w:", 2)) core_win_goto(e, t[2], 0);
	else if (!strncmp(t, "e:", 2)) CHECK(!core_edit_file(e, t + 2, 0));
	/* ref/keys.c has no visual mode: its flag, as 'v' sets it */
	else if (!strcmp(t, "visual")) e->ks.visual = 'v';
	else CHECK(!"command");
	do core_draw(e, g);
	while (syntax_update(e->buf->syn, e->buf->text, NULL));
}

/* Cases listed as known, 25 of 1000.  The core's (package 4, reported):
   the window's top after a split (58 ... 914).  Neovim's own: a status
   line it had no reason to redraw keeps a stale ruler, in insert and
   command-line mode, where the recorder cannot send CTRL-L (29 ... 674);
   a line still rendered in insert mode (489, hypothesis: stale too).  The
   syntax package's: an inline colour Neovim does not give (804).  Not
   copied: where Neovim puts the icon of a short line in a window scrolled
   past its end (101). */
static int known(int n)
{
	static const int k[] = { 58, 259, 455, 482, 488, 534, 547, 738, 761, 815, 914,
	                         29, 39, 115, 214, 233, 247, 517, 582, 650, 660, 674,
	                         489, 804, 101 };
	(void)k; (void)n;
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
			for (int r = 0; r < nrow; r++)
				for (int k = 0; k < 3; k++)
					if (strcmp(row(&g, r, "SBF"[k]), want[r][k])) {
						diff = 1;
						if (!bad || verbose)
							fprintf(stderr, "case %d row %d %c:\n  nvim [%s]\n  core [%s]\n", ncase,
							        r, "SBF"[k], want[r][k], row(&g, r, "SBF"[k]));
					}
			
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
