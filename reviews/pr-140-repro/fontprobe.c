/* Review of PR 140: what src/font costs on Windows itself (or under Wine).
   Prints font_open's time (the first call in the process, then again),
   the fonts the finder numbered, and for each test string the time of its
   first font_shape (the finder's look: MapCharacters, then the scan of
   every family) and of its second (the remembered answer).  Then rows of
   distinct characters no font has: every one is a whole scan.
   Build: winprobe.sh.  Run: fontprobe.exe [family] */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "font.h"

const char *find_file(int i, int *index);   /* src/font/find.h */

static double now(void)
{
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1e6 / (double)f.QuadPart;
}

static int nfonts(void) { int i = 0, x; while (i < 64 && find_file(i, &x)) i++; return i; }

static int put(char *s, unsigned c)
{
	if (c < 0x800) { s[0] = (char)(0xC0 | c >> 6); s[1] = (char)(0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { s[0] = (char)(0xE0 | c >> 12); s[1] = (char)(0x80 | (c >> 6 & 63)); s[2] = (char)(0x80 | (c & 63)); return 3; }
	s[0] = (char)(0xF0 | c >> 18); s[1] = (char)(0x80 | (c >> 12 & 63)); s[2] = (char)(0x80 | (c >> 6 & 63)); s[3] = (char)(0x80 | (c & 63));
	return 4;
}

static Placed o[2048];

/* a row of n distinct characters from c up; the time of its first and second shaping */
static void row(const char *what, unsigned c, int n)
{
	static char s[8192];
	int k = 0;
	for (int i = 0; i < n; i++) k += put(s + k, c + (unsigned)i);
	double t0 = now();
	font_shape((const unsigned char *)s, (size_t)k, o, 2048);
	double t1 = now();
	font_shape((const unsigned char *)s, (size_t)k, o, 2048);
	double t2 = now();
	printf("%-44s first %10.0f us (%.0f us a character), again %6.1f us, fonts %d\n", what, t1 - t0, (t1 - t0) / n, t2 - t1, nfonts());
}

int main(int argc, char **argv)
{
	const char *family = argc > 1 ? argv[1] : "JetBrainsMono Nerd Font Mono,JetBrainsMono NFM,JetBrainsMono Nerd Font,JetBrains Mono";
	setvbuf(stdout, NULL, _IONBF, 0);
	for (int i = 0; i < 6; i++) {
		double t0 = now();
		int r = font_open(family, 16);
		printf("font_open %d: %.0f us (returned %d)\n", i, now() - t0, r);
		if (r) return 1;
	}
	int w, h, b, idx;
	font_cell(&w, &h, &b);
	printf("cell %d x %d, main font %s\n", w, h, find_file(0, &idx));
	static const struct { const char *name, *s; } t[] = {
		{ "ascii 'static int pick'", "static int pick(unsigned a) { return a < b ? after : -1; }" },
		{ "U+2726 black four pointed star", "\xe2\x9c\xa6" },
		{ "U+21BB clockwise open circle arrow", "\xe2\x86\xbb" },
		{ "U+23F5 black medium right triangle", "\xe2\x8f\xb5" },
		{ "U+273B teardrop-spoked asterisk", "\xe2\x9c\xbb" },
		{ "U+4E2D CJK", "\xe4\xb8\xad" },
		{ "U+1D4DD math script N", "\xf0\x9d\x93\x9d" },
		{ "U+1F600 emoji", "\xf0\x9f\x98\x80" },
		{ "U+2764 U+FE0F heart, emoji style", "\xe2\x9d\xa4\xef\xb8\x8f" },
		{ "U+1F468 ZWJ U+1F469 ZWJ U+1F467 family", "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7" },
		{ "U+2500 box drawing (drawn)", "\xe2\x94\x80" },
		{ "U+E0A0 Powerline branch (Nerd Font)", "\xee\x82\xa0" },
		{ "U+F115 Nerd Font folder icon", "\xef\x84\x95" },
		{ "U+0378 unassigned", "\xcd\xb8" },
	};
	for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
		int before = nfonts();
		double t0 = now();
		int n = font_shape((const unsigned char *)t[i].s, strlen(t[i].s), o, 2048);
		double t1 = now();
		const Glyph *g = n ? o[0].g : NULL;
		long ink = 0, tint = 0;
		for (int j = 0; g && j < g->w * g->h; j++) {
			if (!g->color) { ink += g->px[j]; continue; }
			const unsigned char *p = g->px + 4 * j;
			ink += p[3];
			tint += p[0] != p[1] || p[1] != p[2];
		}
		int gw = g ? g->w : 0, gh = g ? g->h : 0, gc = g ? g->color : 0;
		font_shape((const unsigned char *)t[i].s, strlen(t[i].s), o, 2048);
		double t2 = now();
		int after = nfonts();
		printf("%-40s first %9.0f us, again %6.1f us: %d glyph(s), %dx%d%s, ink %ld, tinted pixels %ld",
		       t[i].name, t1 - t0, t2 - t1, n, gw, gh, gc ? " colour" : "", ink, tint);
		for (int f = before; f < after; f++) printf("\n      new font %d: %s #%d", f, find_file(f, &idx), idx);
		puts("");
	}
	{   /* an ordinary row, for scale */
		const char *s = t[0].s;
		double t0 = now();
		for (int i = 0; i < 2000; i++) font_shape((const unsigned char *)s, strlen(s), o, 2048);
		printf("ascii row of %d: %.2f us a shaping (2000)\n", (int)strlen(s), (now() - t0) / 2000);
	}
	row("30 characters no font has (plane 15)", 0xF0000, 30);
	row("100 more", 0xF1000, 100);
	/* past the remembered answers' limit (768): the first row is asked again */
	for (int i = 0; i < 8; i++) row("100 more (filling the answers)", 0xF2000 + 0x100 * (unsigned)i, 100);
	row("the first 30 again, after the answers were emptied", 0xF0000, 30);
	font_close();
	return 0;
}
