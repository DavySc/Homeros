/* What shaping costs per frame, against DESIGN.md's "frame ready under 2 ms
   p99": every file on stdin is paged through 60 rows at a time (each frame
   reshapes all 60 rows, as a page down does), twice; the second pass, with
   every glyph cached, is timed.  With ASCII=1 every non-ASCII character is
   replaced by one x first, which takes font fallback out of the cost.  Build as bench/hitrate.c; run:
     find mathlib4/Mathlib -name '*.lean' | sort | FONTCONFIG_FILE=build/fonts.conf build/frames */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "font.h"

enum { COLS = 400, ROWS = 60 };
static int cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

int main(void)
{
	int ascii = getenv("ASCII") != NULL;
	static Placed out[4 * COLS];
	static char paths[9000][256];
	static float t[1 << 17];
	char line[1 << 16];
	unsigned char row[COLS];
	size_t nf = 0, nt = 0;
	while (nf < 9000 && fgets(paths[nf], 256, stdin)) { paths[nf][strcspn(paths[nf], "\n")] = 0; nf++; }
	if (font_open("JetBrains Mono", 20)) return 1;
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < nf; i++) {
			FILE *f = fopen(paths[i], "r");
			if (!f) continue;
			double frame = 0;
			int r = 0;
			while (fgets(line, sizeof line, f)) {
				size_t n = 0;
				for (char *p = line; *p && *p != '\n' && n < COLS; p++)
					if (*p == '\t') for (int k = 0; k < 4 && n < COLS; k++) row[n++] = ' ';
					else if (ascii && (*p & 0x80)) { if ((*p & 0xC0) != 0x80) row[n++] = 'x'; }
					else row[n++] = (unsigned char)*p;
				struct timespec a, b;
				clock_gettime(CLOCK_MONOTONIC, &a);
				font_shape(row, n, out, 4 * COLS);
				clock_gettime(CLOCK_MONOTONIC, &b);
				frame += (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
				if (++r == ROWS) {
					if (pass && nt < sizeof t / sizeof *t) t[nt++] = (float)frame;
					frame = 0;
					r = 0;
				}
			}
			fclose(f);
		}
	qsort(t, nt, sizeof *t, cmp);
	printf("frames %zu: shaping 60 rows, all glyphs cached: median %.0f us, p99 %.0f us, p99.9 %.0f us, max %.0f us\n",
	       nt, t[nt / 2] / 1e3, t[nt * 99 / 100] / 1e3, t[nt * 999 / 1000] / 1e3, t[nt - 1] / 1e3);
	font_close();
	return 0;
}
