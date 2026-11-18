/* render-markdown's headings against Neovim: replays the cases that
   tests/test_core_markdown.py recorded from Neovim with render-markdown
   (tests/test_core_markdown.txt) and compares every window row's text,
   background and heading colours.  usage: test_core_markdown [FILE] [-v] */
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

int main(int argc, char **argv)
{
	const char *path = "tests/test_core_markdown.txt";
	int verbose = 0, ncase = 0, bad = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	FILE *in = fopen(path, "r"), *md = NULL;
	char dir[] = "/tmp/test_core_markdown.XXXXXX", cwd[4096], keys[64] = "";
	CHECK(in && mkdtemp(dir) && getcwd(cwd, sizeof cwd));
	int rows = 0, cols = 0, nrow = 0;
	static char want[64][3][1024];
	while (fgets(line, sizeof line, in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line + 2, "%d %d", &rows, &cols) == 2 && rows <= 64);
			CHECK(!chdir(dir) && (md = fopen("f.md", "w")) && !chdir(cwd));
			nrow = 0;
		} else if (line[0] == 'L') fprintf(md, "%s\n", line + 2);
		else if (line[0] == 'K') { CHECK(strlen(line + 2) < sizeof keys); strcpy(keys, line + 2); }
		else if (line[0] == 'S' || line[0] == 'B' || line[0] == 'F') {
			int k = line[0] == 'S' ? 0 : line[0] == 'B' ? 1 : 2;
			CHECK(nrow < 64 && strlen(line + 2) < sizeof want[nrow][k]);
			strcpy(want[nrow][k], line + 2);
			if (k == 2) nrow++;
		} else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			fclose(md);
			CHECK(!chdir(dir) && !core_open(&e, "f.md") && !grid_resize(&g, rows, cols));
			CHECK(e.buf->syn);
			/* a first frame tells the core its size, as main.c: before it
			   the window is Neovim's default 80 columns */
			core_draw(&e, &g);
			for (const char *s = keys; *s; s++) keys_feed(&e, (unsigned char)*s, 0);
			/* as main.c: a frame, then the parse it asked for (the injected
			   inline grammar of the lines in view), until nothing changes */
			do core_draw(&e, &g);
			while (syntax_update(e.buf->syn, e.buf->text, NULL));
			ncase++;
			for (int r = 0; r < nrow; r++)
				for (int k = 0; k < 3; k++)
					if (strcmp(row(&g, r, "SBF"[k]), want[r][k])) {
						if (!bad++ || verbose)
							fprintf(stderr, "case %d (keys %s) row %d %c:\n  nvim [%s]\n  core [%s]\n", ncase,
							        keys, r, "SBF"[k], want[r][k], row(&g, r, "SBF"[k]));
						r = nrow;
						break;
					}
			core_free(&e);
			grid_free(&g);
			CHECK(!unlink("f.md") && !chdir(cwd));
		}
	}
	fclose(in);
	rmdir(dir);
	printf("test_core_markdown: %d render-markdown screens: %d differ\n", ncase, bad);
	return bad != 0;
}
