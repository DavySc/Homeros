/* What shaping one row of 100 characters costs when they need a fallback:
   none (U+0378, which no font has), sym (✦, from a later font) or box (─,
   drawn by sprite.c).  Shapes the row 2,000 times after a first pass and
   prints the time per row.  Build and run (README, "Fallback answers"):
     cc -O2 -Iinclude -Itests src/font/bench/fallback.c src/font/font.c src/font/sprite.c src/font/fc.c src/uni/uni.c \
       $(pkg-config --cflags --libs freetype2 harfbuzz fontconfig) -lm -o build/fallback
     build/fallback none|sym|box
   (the font set is the tests', test_font_env.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_font_env.h"
#include "font.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
int main(int argc, char **argv)
{
	font_env();
	font_open(FAMILY, 16);
	static char row[1024]; int n = 0;
	const char *what = argc > 1 ? argv[1] : "none";
	for (int i = 0; i < 100; i++) {
		if (!strcmp(what, "none")) { row[n++] = (char)0xcd; row[n++] = (char)0xb8; }        /* U+0378: no font */
		else if (!strcmp(what, "sym")) { memcpy(row + n, "\xe2\x9c\xa6", 3); n += 3; }   /* ✦ from a fallback font */
		else { memcpy(row + n, "\xe2\x94\x80", 3); n += 3; }                              /* ─ */
	}
	static Placed o[1024];
	font_shape((unsigned char *)row, n, o, 1024);
	double t0 = now();
	for (int k = 0; k < 2000; k++) font_shape((unsigned char *)row, n, o, 1024);
	printf("%s %.1f us/row\n", what, (now() - t0) / 2000);
}
