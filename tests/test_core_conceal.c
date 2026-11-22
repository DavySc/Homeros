/* Conceal against Neovim (package 4): replays the cases that
   tests/test_core_conceal.py recorded (an Org text whose syntax_conceal
   spans, at most 64 a line as draw_line takes them, were made Neovim
   Conceal matches; the cursor placed; sometimes Visual mode) and compares
   each text row's characters.  usage: test_core_conceal [FILE] (default
   the .txt), -v prints every difference; test_core_conceal --spans FILE
   prints the spans of an Org file for the recorder.  A case whose
   recorded spans differ from today's is reported: re-record it. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "syntax.h"
#include "../src/core/core_int.h"

static char line[1 << 16];

static void hex(const char *h, FILE *o, char *s)
{
	for (unsigned b; h[0] && h[1] && sscanf(h, "%2x", &b) == 1; h += 2)
		if (o) fputc((int)b, o);
		else *s++ = (char)b;
	if (s) *s = 0;
}

/* each line's spans as K lines (line a z s-in-hex, "-" for hidden) */
static void spans(Editor *e, FILE *o)
{
	size_t r1, r2;
	while (syntax_update(e->buf->syn, e->buf->text, NULL, &r1, &r2));
	Conceal cs[64];
	for (size_t l = 0; l < text_lines(e->buf->text); l++)
		for (size_t k = 0, n = syntax_conceal(e->buf->syn, l, cs, 64); k < n; k++) {
			fprintf(o, "K %zu %zu %zu ", l, cs[k].a, cs[k].z);
			if (!*cs[k].s) fputc('-', o);
			for (const char *s = cs[k].s; *s; s++) fprintf(o, "%02x", (unsigned char)*s);
			fputc('\n', o);
		}
}

int main(int argc, char **argv)
{
	static Editor e;
	if (argc == 3 && !strcmp(argv[1], "--spans")) {
		CHECK(!core_open(&e, argv[2]));
		spans(&e, stdout);
		core_free(&e);
		return 0;
	}
	const char *path = "tests/test_core_conceal.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	char file[] = "/tmp/core_concealXXXXXX.org";
	int fd = mkstemps(file, 4);
	CHECK(fd >= 0);
	close(fd);
	static char want[200][1024], ks[1 << 16];
	int rows = 0, cols = 0, wrap = 0, n = 0, ncase = 0, bad = 0, vis = 0;
	size_t cl = 0, cc = 0, vl = 0, vc = 0, top = 0, left = 0, skip = 0, nk = 0;
	FILE *f = fopen(path, "r");
	CHECK(f);
	while (fgets(line, sizeof line, f)) {
		if (line[0] == 'K') {
			size_t m = strlen(line);
			CHECK(nk + m < sizeof ks);
			memcpy(ks + nk, line, m + 1);
			nk += m;
			continue;
		}
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line, "C %d %d %d %zu %zu", &rows, &cols, &wrap, &cl, &cc) == 5);
			n = 0, vis = 0, nk = 0, ks[0] = 0;
		} else if (line[0] == 'V') vis = sscanf(line, "V %zu %zu", &vl, &vc) == 2;
		else if (line[0] == 'W') CHECK(sscanf(line, "W %zu %zu %zu", &top, &left, &skip) == 3);
		else if (line[0] == 'F') {
			FILE *o = fopen(file, "w");
			CHECK(o);
			hex(line + 2, o, NULL);
			CHECK(!fclose(o));
		} else if (line[0] == 'R') {
			CHECK(n < 200);
			hex(line + 2, NULL, want[n]);
			for (size_t k = strlen(want[n]); k && want[n][k - 1] == ' '; ) want[n][--k] = 0;
			n++;
		} else if (line[0] == 'E') {
			Grid g = { 0 };
			CHECK(!core_open(&e, file) && !grid_resize(&g, rows, cols));
			static char now[1 << 16];
			now[0] = 0;   /* fmemopen() writes no NUL when nothing is written */
			FILE *o = fmemopen(now, sizeof now, "w");
			CHECK(o);
			spans(&e, o);
			CHECK(!fclose(o));
			int differ = strcmp(now, ks) != 0;
			if (differ) fprintf(stderr, "case %d: the spans changed since it was recorded\n", ncase);
			core_set_wrap(&e, wrap);
			layout_resize(&e, rows, cols);
			if (vis) {
				/* the .py went there first and ran `normal! v`: exec_normal()
				   calls update_topline_cursor() before it, with no redraw,
				   so the core updates as in Insert mode (no drawn() pass;
				   case 64 of seed 1 differs with it).  Visual mode as the
				   core reads it (the keys keep their own) */
				e.ks.visual = 'v', e.ks.vline = e.win->line = vl, e.ks.vcol = e.win->col = vc;
				core_set_want(&e);
				int m = e.mode;
				e.mode = MODE_INSERT;
				core_update(&e);
				e.mode = m;
			}
			e.win->line = cl;
			e.win->col = cc;
			core_set_want(&e);
			core_update(&e);
			core_draw(&e, &g);
			const Win *w = e.win;
			int nw = win_numw(w);
			if (w->top != top || w->left != left || w->skipcol != skip) {
				differ++;
				fprintf(stderr, "case %d: view %zu %zu %zu, nvim %zu %zu %zu\n", ncase, w->top, w->left, w->skipcol, top, left, skip);
			}
			for (int r = 0; r < w->h && r < n; r++) {
				char got[1024];
				size_t k = 0;
				for (int c = nw; c < w->w; c++) {
					const Cell *x = &g.c[(w->y + r) * g.cols + w->x + c];
					if (!x->len) continue;
					memcpy(got + k, grid_text(x), x->len);
					k += x->len;
				}
				while (k && got[k - 1] == ' ') k--;
				got[k] = 0;
				if (strcmp(got, want[r]) && (!differ++ || verbose))
					fprintf(stderr, "case %d (%dx%d%s, cursor %zu,%zu) row %d:\n  nvim [%s]\n  core [%s]\n", ncase, rows, cols,
					        wrap ? " wrap" : "", cl, cc, r, want[r], got);
			}
			bad += differ > 0;
			core_free(&e);
			grid_free(&g);
			ncase++;
		}
	}
	fclose(f);
	unlink(file);
	if (bad) { printf("test_core_conceal: %d of %d cases differ from Neovim\n", bad, ncase); return 1; }
	printf("test_core_conceal: %d cases match Neovim\n", ncase);
	return 0;
}
