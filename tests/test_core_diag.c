/* Diagnostics against Neovim (package 4): replays the cases that
   tests/test_core_diag.py recorded (a text, its diagnostics, a window, the
   cursor, 'leftcol') and compares the view, each window row's characters
   and each cell's diagnostic highlight (u underline, v virtual text, s
   sign; 1 error .. 4 hint; "u3/u2" either: tied underlines).  The diagnostics come from a fake language
   server (test_core_diag.py --serve), as from a real one.
   usage: test_core_diag [FILE] (default the .txt), -v prints every
   difference */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "../src/core/core_int.h"

static char line[1 << 16];

static void hex(const char *h, FILE *o, char *s)
{
	for (unsigned b; h[0] && h[1] && sscanf(h, "%2x", &b) == 1; h += 2)
		if (o) fputc((int)b, o);
		else *s++ = (char)b;
	if (s) *s = 0;
}

/* a cell's code, as the .py records it */
static const char *code(const Cell *x)
{
	static const char *const u[] = { "u1", "u2", "u3", "u4" }, *const v[] = { "v1", "v2", "v3", "v4" },
	                         *const s[] = { "s1", "s2", "s3", "s4" };
	if (x->ul >= FACE_DIAG_UNDER_ERROR && x->ul <= FACE_DIAG_UNDER_HINT) return u[x->ul - FACE_DIAG_UNDER_ERROR];
	if (x->face >= FACE_DIAG_VT_ERROR && x->face <= FACE_DIAG_VT_HINT) return v[x->face - FACE_DIAG_VT_ERROR];
	if (x->face >= FACE_DIAG_ERROR && x->face <= FACE_DIAG_HINT) return s[x->face - FACE_DIAG_ERROR];
	return ".";
}

/* the cells' codes against those recorded, where "u3/u2" takes either */
static int same(const char *got, const char *want)
{
	for (;;) {
		size_t g = strcspn(got, " "), w = strcspn(want, " "), ok = 0;
		for (const char *a = want; a < want + w; a += strcspn(a, "/ ") + (a[strcspn(a, "/ ")] == '/'))
			ok |= strcspn(a, "/ ") == g && !strncmp(a, got, g);
		if (!ok || !got[g] != !want[w]) return 0;
		if (!got[g]) return 1;
		got += g + 1, want += w + 1;
	}
}

int main(int argc, char **argv)
{
	static Editor e;
	const char *path = "tests/test_core_diag.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	/* the server runs in the files' directory: absolute paths */
	static char py[PATH_MAX], txt[PATH_MAX], dir[] = "/tmp/core_diagXXXXXX", file[PATH_MAX + 32];
	CHECK(realpath("tests/test_core_diag.py", py) && realpath(path, txt) && mkdtemp(dir));
	static const char *const roots[] = { NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, NULL };
	LspServer table[2] = { { ".diag", "diag", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	/* one server for all cases: a file of none stays open */
	snprintf(file, sizeof file, "%s/keep.diag", dir);
	FILE *o = fopen(file, "w");
	CHECK(o && !fclose(o));
	Text *kt = text_new("", 0);
	LspDoc *keep = lsp_open(file, kt);
	CHECK(kt && keep);
	static char want[200][4096], codes[200][1 << 14];
	int rows = 0, cols = 0, wrap = 0, n = 0, nd = 0, ncase = 0, bad = 0;
	size_t cl = 0, cc = 0, left = 0, top = 0, wleft = 0, skip = 0, pl = 0, pc = 0;
	FILE *f = fopen(path, "r");
	CHECK(f);
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %zu %zu %zu", &rows, &cols, &wrap, &cl, &cc, &left) == 6);
			n = 0, nd = 0;
		} else if (line[0] == 'D') nd++;
		else if (line[0] == 'W') CHECK(sscanf(line, "W %zu %zu %zu %zu %zu", &top, &wleft, &skip, &pl, &pc) == 5);
		else if (line[0] == 'F') {
			snprintf(file, sizeof file, "%s/%d.diag", dir, ncase);
			FILE *o = fopen(file, "w");
			CHECK(o);
			hex(line + 2, o, NULL);
			CHECK(!fclose(o));
		} else if (line[0] == 'R') {
			CHECK(n < 200);
			hex(line + 2, NULL, want[n]);
			for (size_t k = strlen(want[n]); k && want[n][k - 1] == ' '; ) want[n][--k] = 0;
		} else if (line[0] == 'A') {
			CHECK(strlen(line + 2) < sizeof codes[n]);
			strcpy(codes[n++], line + 2);
		} else if (line[0] == 'E') {
			Grid g = { 0 };
			CHECK(!core_open(&e, file) && !grid_resize(&g, rows, cols));
			int have = 0;
			for (int t = 0; t < 500 && have < nd; t++) {
				loop_once(10);
				lsp_diags(e.buf->lsp, &have);
			}
			CHECK(have == nd);
			core_set_wrap(&e, wrap);
			layout_resize(&e, rows, cols);
			e.win->line = cl;
			e.win->col = cc;
			e.win->left = left;
			core_set_want(&e);
			core_update(&e);
			core_draw(&e, &g);
			const Win *w = e.win;
			int differ = 0;
			if (w->top != top || w->left != wleft || w->skipcol != skip || w->line != pl || w->col != pc) {
				differ++;
				fprintf(stderr, "case %d: view %zu %zu %zu cursor %zu,%zu, nvim %zu %zu %zu cursor %zu,%zu\n", ncase,
				        w->top, w->left, w->skipcol, w->line, w->col, top, wleft, skip, pl, pc);
			}
			for (int r = 0; r < w->h && r < n; r++) {
				static char got[4096], gcodes[1 << 14];
				size_t k = 0, kc = 0;
				const char *last = ".";
				for (int c = 0; c < w->w; c++) {
					const Cell *x = &g.c[(w->y + r) * g.cols + w->x + c];
					/* a double-width character's right half: its left's */
					const char *cd = x->len ? code(x) : last;
					kc += (size_t)sprintf(gcodes + kc, "%s%s", c ? " " : "", cd);
					last = cd;
					if (!x->len) continue;
					memcpy(got + k, grid_text(x), x->len);
					k += x->len;
				}
				while (k && got[k - 1] == ' ') k--;
				got[k] = 0;
				if ((strcmp(got, want[r]) || !same(gcodes, codes[r])) && (!differ++ || verbose))
					fprintf(stderr, "case %d (%dx%d%s, cursor %zu,%zu) row %d:\n  nvim [%s]\n  core [%s]\n  nvim %s\n  core %s\n",
					        ncase, rows, cols, wrap ? " wrap" : "", cl, cc, r, want[r], got, codes[r], gcodes);
			}
			bad += differ > 0;
			core_free(&e);
			grid_free(&g);
			unlink(file);
			ncase++;
		}
	}
	fclose(f);
	lsp_close(keep);
	text_free(kt);
	snprintf(file, sizeof file, "%s/keep.diag", dir);
	unlink(file);
	rmdir(dir);
	if (bad) { printf("test_core_diag: %d of %d cases differ from Neovim\n", bad, ncase); return 1; }
	printf("test_core_diag: %d cases match Neovim\n", ncase);
	return 0;
}
