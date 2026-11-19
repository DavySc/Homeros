/* Which glyphs start left of their cell (pen x + left bearing < 0), so that
   PR 68's clip at the span's start lets them draw over the cell before:
   every code point of the ranges below shaped alone and after "a", and the
   ASCII pairs and triples (ligatures).  Usage: bear PX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
static int enc(unsigned c, unsigned char *s)
{
	if (c < 0x80) { s[0] = (unsigned char)c; return 1; }
	if (c < 0x800) { s[0] = (unsigned char)(0xc0 | c >> 6); s[1] = (unsigned char)(0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { s[0] = (unsigned char)(0xe0 | c >> 12); s[1] = (unsigned char)(0x80 | (c >> 6 & 63)); s[2] = (unsigned char)(0x80 | (c & 63)); return 3; }
	s[0] = (unsigned char)(0xf0 | c >> 18); s[1] = (unsigned char)(0x80 | (c >> 12 & 63)); s[2] = (unsigned char)(0x80 | (c >> 6 & 63)); s[3] = (unsigned char)(0x80 | (c & 63)); return 4;
}
int main(int argc, char **argv)
{
	int px = argc > 1 ? atoi(argv[1]) : 16, cw, ch, cb;
	if (font_open("JetBrains Mono", px)) return 2;
	font_cell(&cw, &ch, &cb);
	printf("cell %dx%d\n", cw, ch);
	static const unsigned R[][2] = { {0x21,0x7e},{0xa1,0x17f},{0x370,0x3ff},{0x2000,0x2bff},{0x1d400,0x1d7ff},{0x1f600,0x1f64f} };
	Placed pl[16];
	int neg = 0, tot = 0, worst = 0;
	for (size_t k = 0; k < sizeof R / sizeof *R; k++)
		for (unsigned c = R[k][0]; c <= R[k][1]; c++) {
			unsigned char s[8] = "a";
			int n = enc(c, s + 1), m = font_shape(s, (size_t)n + 1, pl, 16);
			for (int i = 0; i < m; i++) {
				if (pl[i].cluster != 1 || !pl[i].g->w) continue;
				int x = pl[i].x + pl[i].g->left;
				tot++;
				if (x < 0) { neg++; if (x < worst) worst = x; if (x <= -2 || c < 0x80) printf("U+%04X starts %d px left of its cell (w %d)\n", c, x, pl[i].g->w); }
			}
		}
	printf("single: %d of %d glyphs start left of their cell, worst %d px\n", neg, tot, worst);
	/* ASCII pairs: a ligature's glyph on the last cell reaching back; does
	   any reach back past the pair's first cell? */
	int lig = 0, past = 0;
	for (int a = 0x21; a < 0x7f; a++) for (int b = 0x21; b < 0x7f; b++) {
		unsigned char s[3] = { 'a', (unsigned char)a, (unsigned char)b };
		int m = font_shape(s, 3, pl, 16);
		for (int i = 0; i < m; i++) {
			if (!pl[i].g->w || pl[i].cluster == 0) continue;
			int x = (int)pl[i].cluster * cw + pl[i].x + pl[i].g->left;   /* from the run's start */
			if (pl[i].x + pl[i].g->left < -1) lig++;
			if (x < cw) { past++; printf("pair %c%c: glyph of cell %zu starts %d px inside the 'a' before it\n", a, b, pl[i].cluster, cw - x); }
		}
	}
	printf("pairs: %d glyphs reach back over 1 px, %d reach into the cell before the pair\n", lig, past);
	font_close();
	return 0;
}
