/* font.h on Windows (src/font/dwrite.c), under Wine with the host's fonts
   (CI's apt line): the main font is the first family of the list that is
   installed, and with none of them some font still opens; characters the
   main font lacks draw from other fonts: through Windows' fallback list
   (MapCharacters) or, where Wine's short list has nothing (↻ ⏵), the scan
   of every family; a question two fonts answer is not given the first;
   a character outside the BMP (surrogates); box drawing
   is sprite.c's, the cell's size.  What Wine cannot show: Segoe UI Emoji's
   colour, and which font real Windows' list gives (Davy's machine).
   Runs on Windows or under Wine (make win); elsewhere it says so. */
#ifdef _WIN32
#include <windows.h>
#include <string.h>
#include "check.h"
#include "font.h"

const char *find_file(int i, int *index);   /* src/font/find.h, the finder's own */
int find_font(const unsigned *cp, unsigned n, int after, int color);

static Placed o[16];

/* whether font i is JetBrains Mono's file, whose path Windows may give in
   upper case (C:\WINDOWS\FONTS\JETBRAINSMONO-REGULAR.TTF, reviews/pr-140.md) */
static int jetbrains(int i)
{
	int idx;
	const char *f = find_file(i, &idx);
	return f && (strstr(f, "JetBrainsMono") || strstr(f, "JETBRAINSMONO")) && idx == 0;
}
static int shape(const char *s) { return font_shape((const unsigned char *)s, strlen(s), o, 16); }

/* whether s (one character) draws: one glyph, not the main font's
   .notdef, with ink */
static int draws(const char *s, const Glyph *notdef)
{
	if (shape(s) != 1 || o[0].g == notdef) return 0;
	long ink = 0;
	for (int i = 0; i < o[0].g->w * o[0].g->h * (o[0].g->color ? 4 : 1); i++) ink += o[0].g->px[i];
	return ink > 0;
}

int main(void)
{
	int idx, w, h, b;
	CHECK(font_open("No Such Family,JetBrains Mono", 16) == 0);
	CHECK(jetbrains(0));
	font_cell(&w, &h, &b);
	CHECK(w == 10 && h > 16 && b > 0 && b < h);   /* JetBrains Mono's advance is 0.6 em */
	CHECK(shape("\xcd\xb8") == 1);   /* U+0378: unassigned */
	const Glyph *notdef = o[0].g;
	static const char *const more[] = {
		"\xe2\x9c\xa6",       /* ✦ (Claude Code's model mark) */
		"\xe2\x86\xbb",       /* ↻ (its reset time) */
		"\xe2\x8f\xb5",       /* ⏵ (its auto mode) */
		"\xe4\xb8\xad",       /* 中 */
		"\xf0\x9d\x93\x9d",   /* 𝓝, outside the BMP */
	};
	for (unsigned i = 0; i < sizeof more / sizeof *more; i++) {
		if (!draws(more[i], notdef)) fprintf(stderr, "test_win_font: %s does not draw\n", more[i]);
		CHECK(draws(more[i], notdef));
	}
	CHECK(find_file(1, &idx) && !jetbrains(1));   /* from another font */
	CHECK(shape("\xe2\x94\x80") == 1 && o[0].g->w == w && o[0].g->h == h);        /* ─ is drawn, not a font's */
	/* a question Windows' list answers only in part (中 from a CJK font,
	   𝓝 not) is not given that font */
	static const unsigned q[] = { 0x4E2D, 0x1D4DD };
	CHECK(find_font(q, 2, 0, 0) != find_font(q, 1, 0, 0));
	/* the same again comes from the caches: no new glyph drawn */
	unsigned long hit0, miss0, hit1, miss1;
	font_stats(&hit0, &miss0);
	for (unsigned i = 0; i < sizeof more / sizeof *more; i++) CHECK(draws(more[i], notdef));
	font_stats(&hit1, &miss1);
	CHECK(miss1 == miss0 && hit1 == hit0 + sizeof more / sizeof *more);
	/* none of the families installed: still a font, whatever draws an M */
	CHECK(font_open("No Such Family", 16) == 0 && draws("M", NULL));
	font_close();
	puts("test_win_font: family list, fallback by Windows' list and by scan, astral, box drawing drawn, caches");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_win_font: Windows only (make win runs it under Wine)"); return 0; }
#endif
