/* Review of PR 56: tests/test_core_markdown.c with commands besides keys,
   every row compared (not the last: the command line), and the cursor's
   cell.  Cases come from rec.py (live Neovim with render-markdown).
   A case: C rows cols / L line... / T token (one per line: k:keys with
   <Esc>, wrap, nowrap, sp, vs, w:<c>, C-e, C-y, visual, e:file, redraw (CTRL-L in Neovim, nothing here)) / X row col (the
   terminal's cursor) / S B F per row / E.
   usage: mdcmp FILE [-v] */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "syntax.h"
#include "../src/core/core_int.h"

static char line[8192];

static char letter(const unsigned char *c, const char *const *hex, const char *set)
{
	char h[8];
	snprintf(h, sizeof h, "%02x%02x%02x", c[0], c[1], c[2]);
	for (int i = 0; i < 6; i++) if (!strcmp(h, hex[i])) return set[i];
	return '.';
}
static const char *const BG[] = { "5a3d50", "5c4847", "5b5552", "44554e", "364d63", "484b68" };
static const char *const FG[] = { "f38ba8", "fab387", "f9e2af", "a6e3a1", "74c7ec", "b4befe" };

static const char *row(const Grid *g, int r, char what)
{
	static char b[4096];
	size_t n = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		if (!x->len && c) x--;
		const Face *f = &faces[x->face], *bg = x->bg ? &faces[x->bg] : f;
		if (what == 'S') { if (x == &g->c[r * g->cols + c]) { memcpy(b + n, grid_text(x), x->len); n += x->len; } }
		else if (what == 'B') b[n++] = bg->style & FACE_BG ? letter(bg->bg, BG, "RPYGSL") : '.';
		else b[n++] = f->style & FACE_FG && !(x->len == 1 && x->ch[0] == ' ') ? letter(f->fg, FG, "rpygsl") : '.';
	}
	b[n] = 0;
	return b;
}

static void token(Editor *e, const char *t)
{
	if (!strncmp(t, "k:", 2)) {
		for (const char *s = t + 2; *s; s++)
			if (!strncmp(s, "<Esc>", 5)) { keys_feed(e, KEY_ESC, 0); s += 4; }
			else keys_feed(e, (unsigned char)*s, 0);
	}
	else if (!strcmp(t, "wrap") || !strcmp(t, "nowrap")) core_set_wrap(e, t[0] == 'w');
	else if (!strcmp(t, "sp") || !strcmp(t, "vs")) core_split(e, t[0] == 'v', 0);
	else if (!strncmp(t, "w:", 2)) core_win_goto(e, t[2], 0);
	else if (!strcmp(t, "C-e")) core_scroll(e, 1);
	else if (!strcmp(t, "C-y")) core_scroll(e, -1);
	else if (!strcmp(t, "visual")) e->ks.visual = 1;
	else if (!strcmp(t, "redraw")) {}
	else if (!strncmp(t, "e:", 2)) core_edit_file(e, t + 2, 0);
	else CHECK(!"token");
	core_update(e);
}

int main(int argc, char **argv)
{
	const char *path = NULL;
	int verbose = 0, ncase = 0, bad = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	FILE *in = fopen(path, "r"), *md = NULL;
	char dir[] = "/tmp/mdcmp.XXXXXX", cwd[4096];
	static char toks[64][256], name[256];
	int ntok = 0;
	CHECK(in && mkdtemp(dir) && getcwd(cwd, sizeof cwd));
	int rows = 0, cols = 0, nrow = 0, xr = -1, xc = -1;
	static char want[64][3][4096];
	while (fgets(line, sizeof line, in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			name[0] = 0;
			CHECK(sscanf(line + 2, "%d %d %255[^\n]", &rows, &cols, name) >= 2 && rows <= 64);
			CHECK(!chdir(dir) && (md = fopen("f.md", "w")) && !chdir(cwd));
			nrow = ntok = 0; xr = xc = -1;
		} else if (line[0] == 'L') fprintf(md, "%s\n", line + 2);
		else if (line[0] == 'T') { CHECK(ntok < 64 && strlen(line + 2) < 256); strcpy(toks[ntok++], line + 2); }
		else if (line[0] == 'X') sscanf(line + 2, "%d %d", &xr, &xc);
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
			do core_draw(&e, &g);
			while (syntax_update(e.buf->syn, e.buf->text, NULL));
			for (int i = 0; i < ntok; i++) {
				token(&e, toks[i]);
				do core_draw(&e, &g);
				while (syntax_update(e.buf->syn, e.buf->text, NULL));
			}
			ncase++;
			int diff = 0;
			for (int r = 0; r < nrow && !diff; r++)
				for (int k = 0; k < 3; k++)
					if (strcmp(row(&g, r, "SBF"[k]), want[r][k])) {
						printf("case %d [%s] row %d %c:\n  nvim [%s]\n  core [%s]\n", ncase,
						        name, r, "SBF"[k], want[r][k], row(&g, r, "SBF"[k]));
						diff = 1;
						if (verbose) for (int q = 0; q < nrow; q++) {
							printf("   n|%s|\n", want[q][0]); printf("   c|%s|\n", row(&g, q, 'S'));
							printf("   n|%s|\n", want[q][1]); printf("   c|%s|\n", row(&g, q, 'B'));
						}
						break;
					}
			if (!diff && xr >= 0 && e.mode != MODE_CMDLINE && (g.crow != xr || g.ccol != xc)) {
				printf("case %d [%s] cursor: nvim %d,%d core %d,%d\n", ncase, name, xr, xc, g.crow, g.ccol);
				diff = 1;
			}
			bad += diff;
			core_free(&e);
			grid_free(&g);
			CHECK(!unlink("f.md") && !chdir(cwd));
		}
	}
	fclose(in);
	rmdir(dir);
	printf("mdcmp: %d screens: %d differ\n", ncase, bad);
	return bad != 0;
}
