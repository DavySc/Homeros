/* The Visual area against Neovim (package 4): replays the cases that
   tests/test_core_visual.py recorded and compares the cells of each text
   row drawn with FACE_VISUAL (a double-width character's right half as its
   left), and the mode on the last row.  usage: test_core_visual [FILE]
   (default tests/test_core_visual.txt); with -v, prints every difference. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "../src/core/core_int.h"

static char line[1 << 16];

int main(int argc, char **argv)
{
	const char *path = "tests/test_core_visual.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	FILE *f = fopen(path, "r");
	CHECK(f);
	char file[] = "/tmp/core_visualXXXXXX";
	int fd = mkstemp(file);
	CHECK(fd >= 0);
	close(fd);
	static char want[200][256];
	int rows = 0, cols = 0, wrap = 0, mode = 0, eol = 0, n = 0, ncase = 0, bad = 0;
	size_t vl = 0, vc = 0, cl = 0, cc = 0, top = 0, left = 0, skip = 0;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %d %zu %zu %zu %zu %d", &rows, &cols, &wrap, &mode, &vl, &vc, &cl, &cc, &eol) == 9);
			n = 0;
		} else if (line[0] == 'W') CHECK(sscanf(line, "W %zu %zu %zu", &top, &left, &skip) == 3);
		else if (line[0] == 'F') {
			FILE *o = fopen(file, "w");
			CHECK(o);
			for (const char *h = line + 2; h[0] && h[1]; h += 2) {
				unsigned b;
				CHECK(sscanf(h, "%2x", &b) == 1);
				fputc((int)b, o);
			}
			CHECK(!fclose(o));
		} else if (line[0] == 'A') {
			CHECK(n < 200 && strlen(line) >= 1);
			strcpy(want[n++], line[1] ? line + 2 : "");
		} else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			CHECK(!core_open(&e, file) && !grid_resize(&g, rows, cols));
			core_set_wrap(&e, wrap);
			/* the .py's steps after the start: the cursor to VIsual, v (":normal!" ends with
			   update_topline_cursor()), the cursor to its place, $ (another
			   ":normal!"), a redraw */
			layout_resize(&e, rows, cols);
			e.win->line = vl;
			e.win->col = vc;
			core_update(&e);
			e.ks.visual = mode;
			e.ks.vline = vl;
			e.ks.vcol = vc;
			e.win->line = cl;
			e.win->col = cc;
			core_set_want(&e);
			if (eol) { e.win->want = WANT_EOL; core_update(&e); }
			core_draw(&e, &g);
			const Win *w = e.win;
			int nw = win_numw(w), differ = 0;
			if (w->top != top || w->left != left || w->skipcol != skip) {
				differ++;
				fprintf(stderr, "case %d: view %zu %zu %zu, nvim %zu %zu %zu\n", ncase, w->top, w->left, w->skipcol, top, left, skip);
			}
			for (int r = 0; r < w->h && r < n; r++) {
				char got[256];
				int k = 0;
				for (int c = nw; c < w->w; c++) {
					const Cell *x = &g.c[(w->y + r) * g.cols + w->x + c];
					if (!x->len && c > nw) x--;
					got[k++] = x->bg == FACE_VISUAL ? 'V' : '.';
				}
				while (k && got[k - 1] == '.') k--;
				got[k] = 0;
				if (strcmp(got, want[r]) && (!differ++ || verbose))
					fprintf(stderr, "case %d (%dx%d%s, %c %zu,%zu to %zu,%zu%s) row %d:\n  nvim [%s]\n  core [%s]\n", ncase, rows, cols,
					        wrap ? " wrap" : "", mode == 22 ? 'B' : mode, vl, vc, cl, cc, eol ? " $" : "", r, want[r], got);
			}
			const char *m = mode == 'v' ? "-- VISUAL --" : mode == 'V' ? "-- VISUAL LINE --" : "-- VISUAL BLOCK --";
			for (size_t i = 0; m[i]; i++)
				if (g.c[(g.rows - 1) * g.cols + (int)i].ch[0] != (unsigned char)m[i]) { if (!differ++ || verbose) fprintf(stderr, "case %d: no %s\n", ncase, m); break; }
			bad += differ > 0;
			core_free(&e);
			grid_free(&g);
			ncase++;
		}
	}
	fclose(f);
	unlink(file);
	if (bad) { printf("test_core_visual: %d of %d cases differ from Neovim\n", bad, ncase); return 1; }
	printf("test_core_visual: %d cases match Neovim\n", ncase);
	return 0;
}
