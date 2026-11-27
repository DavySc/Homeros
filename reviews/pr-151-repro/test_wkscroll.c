/* PR 151 review probe: the panel's rows against a model of which-key's
   scrolled window (win.lua's scroll(): delta = ceil(+-height / 2), topline
   clamped to [1, lines - height + 1]), for many sizes, cursor rows and
   key sequences.  MODEL=pr uses the PR's symmetric (H + 1) / 2 instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"
#include "loop.h"

static void rowtext(const Grid *g, int r, char *b)
{
	size_t n = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		memcpy(b + n, grid_text(x), x->len);
		n += x->len;
	}
	b[n] = 0;
}

static char lines[64][2048];
static int nlines;

int main(int argc, char **argv)
{
	int pr = argc > 1 && !strcmp(argv[1], "pr"), burst = argc > 2;
	char path[] = "/tmp/wkscrollXXXXXX";
	int fd = mkstemp(path);
	if (fd < 0 || write(fd, "abcb\n", 5) != 5 || close(fd)) return 2;
	static Editor e;
	if (core_open(&e, path)) return 2;
	static Grid g, tall;
	long draws = 0, bad = 0, badhelp = 0;
	unsigned seed = 1;
	for (int level = 0; level < 2; level++)
	for (int cols = 20; cols <= 200; cols += 9) {
		/* the whole panel, from a tall grid */
		grid_resize(&tall, 100, cols);
		tall.crow = 0;
		keys_input(&e, ' ', 0);
		long long t0 = loop_now();
		while (loop_now() - t0 < 300000000LL) loop_once(10);
		if (level) keys_input(&e, 'g', 0);
		grid_clear(&tall);
		keys_draw(&e, &tall);
		int top = 0;
		while (top < 100 && tall.c[top * cols].bg != FACE_NORMAL_FLOAT) top++;
		nlines = 98 - top + 1;          /* rows top..98; 98 is the footer = the empty line */
		for (int i = 0; i < nlines - 1; i++) rowtext(&tall, top + i, lines[i]);
		memset(lines[nlines - 1], ' ', cols), lines[nlines - 1][cols] = 0;
		for (int rows = 4; rows <= 40; rows++) {
			grid_resize(&g, rows, cols);
			for (int crow = 0; crow < rows; crow++) {
				g.crow = crow;
				int wtop = 0;
				/* reset the scroll: <BS> to SPC, or g then <BS> */
				if (level) { keys_input(&e, KEY_BS, 0); keys_input(&e, 'g', 0); }
				else { keys_input(&e, 'g', 0); keys_input(&e, KEY_BS, 0); }
				for (int step = 0; step < 8; step++) {
					grid_clear(&g);
					keys_draw(&e, &g);
					draws++;
					int t = 0;
					while (t < rows && g.c[t * cols].bg != FACE_NORMAL_FLOAT) t++;
					if (t == rows) break;       /* no room */
					int H = rows - t;
					if (g.c[(rows - 1) * cols].bg != FACE_NORMAL_FLOAT) H--;
					int max = nlines - H;
					if (max < 0) max = 0;
					if (wtop > max) wtop = max;
					if (wtop < 0) wtop = 0;
					char b[2048];
					for (int i = 0; i < H - 1; i++) {
						rowtext(&g, t + i, b);
						if (strcmp(b, lines[wtop + i])) {
							if (bad++ < 5) fprintf(stderr, "level %d %dx%d crow %d step %d H %d lines %d wtop %d row %d:\n [%s]\n [%s]\n", level, rows, cols, crow, step, H, nlines, wtop, i, b, lines[wtop + i]);
							break;
						}
					}
					rowtext(&g, t + H - 1, b);
					if (cols >= 60 && !!strstr(b, "scroll") != (H < nlines)) badhelp++;
					int n = burst ? 1 + rand_r(&seed) % 3 : 1;
					for (int j = 0; j < n; j++) {
						int up = rand_r(&seed) % 2;
						keys_input(&e, up ? 21 : 4, 0);
						if (pr) wtop += (up ? -1 : 1) * ((H + 1) / 2);
						else wtop += up ? -(H / 2) : (H + 1) / 2;   /* ceil(+-H / 2) */
						if (burst) { if (wtop < 0) wtop = 0; if (wtop > max) wtop = max; }
					}
				}
			}
		}
		keys_input(&e, KEY_ESC, 0);
		keys_input(&e, KEY_ESC, 0);
	}
	printf("model %s%s: %ld draws, %ld differ, %ld wrong help\n", pr ? "PR" : "which-key", burst ? " (bursts of keys between draws)" : "", draws, bad, badhelp);
	unlink(path);
	return 0;
}
