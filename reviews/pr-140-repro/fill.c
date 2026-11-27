/* Review of PR 140, finding 1: one emoji-style cluster whose character
   many plain fonts have (a copyright sign with U+FE0F) makes dwrite.c's
   scan number every such family while it looks for a colour font; the 64
   numbers run out, and from then on no character can get a fallback font.
   Prints the fonts numbered and whether CJK still draws; exit 1 if the
   same character drew in a fresh font_open but not after the cluster.
   Build: winprobe.sh.  Run: fill.exe (Windows or Wine). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "font.h"

const char *find_file(int i, int *index);   /* src/font/find.h */

static Placed o[64];
static int nfonts(void) { int i = 0, x; while (i < 64 && find_file(i, &x)) i++; return i; }
static const Glyph *shape1(const char *s) { return font_shape((const unsigned char *)s, strlen(s), o, 64) ? o[0].g : NULL; }
static double now(void)
{
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1e6 / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
	const char *family = argc > 1 ? argv[1] : "JetBrains Mono";
	/* 中 ✦ ↻ 𝓝 (Hebrew) (Devanagari) 𓀀 ᚠ ⠿ ♔ ก ა አ Ꭰ ཀ ក 🂡 ⌘ ␀ ꙮ ᜀ 𐎀 */
	static const char *const later[] = { "\xe4\xb8\xad", "\xe2\x9c\xa6", "\xe2\x86\xbb", "\xf0\x9d\x93\x9d", "\xd7\x90", "\xe0\xa4\x85", "\xf0\x93\x80\x80", "\xe1\x9a\xa0", "\xe2\xa0\xbf", "\xe2\x99\x94", "\xe0\xb8\x81", "\xe1\x83\x90", "\xe1\x8a\xa0", "\xe1\x8e\xa0", "\xe0\xbd\x80", "\xe1\x9e\x80", "\xf0\x9f\x82\xa1", "\xe2\x8c\x98", "\xe2\x90\x80", "\xea\x99\xae", "\xe1\x9c\x80", "\xf0\x90\x8e\x80" };
	enum { N = sizeof later / sizeof *later };
	static const char *const style[] = { "\xc2\xa9\xef\xb8\x8f", "\xe2\x84\xa2\xef\xb8\x8f", "\xe2\x9a\xa0\xef\xb8\x8f",
	                                     "\xe2\x9c\x94\xef\xb8\x8f", "\xe2\x9d\xa4\xef\xb8\x8f", "\xe2\x96\xb6\xef\xb8\x8f",
	                                     "A\xef\xb8\x8f" };   /* ©️ ™️ ⚠️ ✔️ ❤️ ▶️, and A with U+FE0F, which no colour font has */
	int fresh[N], bad = 0;
	if (font_open(family, 16)) return 2;
	const Glyph *notdef = shape1("\xcd\xb8");
	for (int i = 0; i < N; i++) fresh[i] = shape1(later[i]) != notdef;
	printf("fresh: %d fonts numbered after %d plain characters\n", nfonts(), N);
	if (font_open(family, 16)) return 2;
	notdef = shape1("\xcd\xb8");
	for (unsigned i = 0; i < sizeof style / sizeof *style; i++) {
		int before = nfonts();
		double t0 = now();
		const Glyph *g = shape1(style[i]);
		printf("emoji-style cluster %u: %.0f us, %s, fonts numbered %d -> %d\n", i, now() - t0,
		       g && g->color ? "colour" : g == notdef ? "no glyph" : "plain", before, nfonts());
	}
	for (int i = 0; i < N; i++) {
		double t0 = now();
		int ok = shape1(later[i]) != notdef;
		printf("plain character %d: fresh %s, after the clusters %s (%.0f us)\n", i, fresh[i] ? "draws" : "no glyph", ok ? "draws" : "NO GLYPH", now() - t0);
		bad |= fresh[i] && !ok;
	}
	font_close();
	return bad;
}
