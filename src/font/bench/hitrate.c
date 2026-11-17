/* The acceptance's glyph cache hit rate, and what shaping a row costs.
   Draws files as the window would: each line (tabs as 4 spaces, cut at
   COLS bytes) is one run, one row.  Prints, per file and in total, cache
   hits and misses; with -cold the cache starts empty for each file (font_open
   per file: the first screen of a freshly started editor).  Build and run:
     cc -O2 -Iinclude src/font/bench/hitrate.c src/font/font.c src/uni/uni.c \
       $(pkg-config --cflags --libs freetype2 harfbuzz fontconfig) -o build/hitrate
     find mathlib4/Mathlib -name '*.lean' | sort | FONTCONFIG_FILE=build/fonts.conf build/hitrate [-cold] [-rows N]
   (build/fonts.conf is the one the font tests write.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "font.h"

enum { COLS = 400 };

static int cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	int cold = 0, rows = 1 << 30;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-cold")) cold = 1;
		else if (!strcmp(argv[i], "-rows") && i + 1 < argc) rows = atoi(argv[++i]);
	static Placed out[4 * COLS];
	char path[4096], line[1 << 16];
	unsigned char row[COLS];
	unsigned long th = 0, tm = 0, nrows = 0, files = 0, worst_files = 0;
	double ns = 0, worst = 1;
	static float t[1 << 22];   /* each row's time, for the median and p99 */
	if (!cold && font_open("JetBrains Mono", 20)) return 1;
	while (fgets(path, sizeof path, stdin)) {
		path[strcspn(path, "\n")] = 0;
		FILE *f = fopen(path, "r");
		if (!f) continue;
		if (cold && font_open("JetBrains Mono", 20)) return 1;
		unsigned long h0, m0, h1, m1;
		font_stats(&h0, &m0);
		for (int r = 0; r < rows && fgets(line, sizeof line, f); r++) {
			size_t n = 0;
			for (char *p = line; *p && *p != '\n' && n < COLS; p++)
				if (*p == '\t') for (int k = 0; k < 4 && n < COLS; k++) row[n++] = ' ';
				else row[n++] = (unsigned char)*p;
			struct timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a);
			font_shape(row, n, out, 4 * COLS);
			clock_gettime(CLOCK_MONOTONIC, &b);
			double d = (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
			ns += d;
			if (nrows < sizeof t / sizeof *t) t[nrows] = (float)d;
			nrows++;
		}
		fclose(f);
		font_stats(&h1, &m1);
		th += h1 - h0;
		tm += m1 - m0;
		files++;
		if (h1 - h0 + m1 - m0) {
			double r = (double)(h1 - h0) / (double)(h1 - h0 + m1 - m0);
			if (r < worst) worst = r;
			if (r < 0.9) worst_files++;
		}
	}
	printf("files %lu rows %lu lookups %lu hits %lu misses %lu hit rate %.5f%%\n",
	       files, nrows, th + tm, th, tm, 100.0 * (double)th / (double)(th + tm));
	size_t m = nrows < sizeof t / sizeof *t ? nrows : sizeof t / sizeof *t;
	qsort(t, m, sizeof *t, cmp);
	printf("worst file %.2f%%, files under 90%%: %lu; per row (with the misses' drawing): mean %.2f us, median %.2f, p99 %.2f, max %.0f\n",
	       100 * worst, worst_files, ns / 1e3 / (double)nrows, t[m / 2] / 1e3, t[m * 99 / 100] / 1e3, t[m - 1] / 1e3);
	font_close();
	return 0;
}
