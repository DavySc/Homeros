/* 'hlsearch' against Neovim (package 4): replays the cases that
   tests/test_core_search.py recorded (a search typed with /, then the
   cursor moved) and compares the cells of each text row drawn in
   FACE_SEARCH and FACE_CUR_SEARCH (a double-width character's right half
   as its left).  usage: test_core_search [FILE] (default the .txt); with
   -v, prints every difference. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "uni.h"
#include "../src/core/core_int.h"

static char line[1 << 16];

static void hex(const char *h, FILE *o, char *s)
{
	for (unsigned b; h[0] && h[1] && sscanf(h, "%2x", &b) == 1; h += 2)
		if (o) fputc((int)b, o);
		else *s++ = (char)b;
	if (s) *s = 0;
}

int main(int argc, char **argv)
{
	const char *path = "tests/test_core_search.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	char file[] = "/tmp/core_searchXXXXXX", pat[256] = "";
	int fd = mkstemp(file);
	CHECK(fd >= 0);
	close(fd);
	static char want[200][256];
	int rows = 0, cols = 0, wrap = 0, n = 0, ncase = 0, bad = 0;
	size_t cl = 0, cc = 0, top = 0, left = 0, skip = 0;
	FILE *f = fopen(path, "r");
	CHECK(f);
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %zu %zu", &rows, &cols, &wrap, &cl, &cc) == 5);
			n = 0;
		} else if (line[0] == 'P') hex(line + 2, NULL, pat);
		else if (line[0] == 'W') CHECK(sscanf(line, "W %zu %zu %zu", &top, &left, &skip) == 3);
		else if (line[0] == 'F') {
			FILE *o = fopen(file, "w");
			CHECK(o);
			hex(line + 2, o, NULL);
			CHECK(!fclose(o));
		} else if (line[0] == 'A') {
			CHECK(n < 200);
			strcpy(want[n++], line[1] ? line + 2 : "");
		} else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			CHECK(!core_open(&e, file) && !grid_resize(&g, rows, cols));
			core_set_wrap(&e, wrap);
			layout_resize(&e, rows, cols);
			/* the .py's steps: 1G^ (the start), /pattern Enter, the cursor
			   to its place, a redraw */
			keys_feed(&e, '/', 1);
			const unsigned char *u = (const unsigned char *)pat;
			for (size_t i = 0, m = strlen(pat); i < m; i += uni_len(u, m, i)) keys_feed(&e, (int)uni_cp(u, m, i), 1);
			keys_feed(&e, KEY_ENTER, 0);
			e.win->line = cl;
			e.win->col = cc;
			core_set_want(&e);
			core_update(&e);
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
					got[k++] = x->bg == FACE_SEARCH ? 'S' : x->bg == FACE_CUR_SEARCH ? 'C' : '.';
				}
				while (k && got[k - 1] == '.') k--;
				got[k] = 0;
				if (strcmp(got, want[r]) && (!differ++ || verbose))
					fprintf(stderr, "case %d (%dx%d%s, /%s, cursor %zu,%zu) row %d:\n  nvim [%s]\n  core [%s]\n", ncase, rows, cols,
					        wrap ? " wrap" : "", pat, cl, cc, r, want[r], got);
			}
			bad += differ > 0;
			core_free(&e);
			grid_free(&g);
			ncase++;
		}
	}
	fclose(f);
	unlink(file);
	if (bad) { printf("test_core_search: %d of %d cases differ from Neovim\n", bad, ncase); return 1; }
	printf("test_core_search: %d cases match Neovim\n", ncase);
	return 0;
}
