/* Review probes for package 7.  Build (from the repository root, after
   `make test TESTS=font` wrote build/fonts.conf):
     gcc -g -O1 -fsanitize=address,undefined -Iinclude -Itests reviews/package-7-repro/probe.c \
       src/font/font.c ref/uni.c $(pkg-config --cflags --libs freetype2 harfbuzz fontconfig) -o build/probe
     build/probe open|shapes|emoji|allocs|scripts|nofont|long|rss|wide FILE
   (allocs and scripts need the sanitizer build; time open, nofont and rss
   with -O2 and no sanitizer, adding
   -Wl,--defsym=__sanitizer_install_malloc_and_free_hooks=0).  It writes
   build/fonts.conf like the font tests, so do not run it with HOMEROS_FONTS
   set while `make check` runs in the same tree.  PX and FAMILY override the
   size and the family. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_font_env.h"
#include "font.h"

int __sanitizer_install_malloc_and_free_hooks(void (*)(const volatile void *, size_t), void (*)(const volatile void *));
static unsigned long allocs, bytes;
static void on_malloc(const volatile void *p, size_t n) { (void)p; allocs++; bytes += n; }
static void on_free(const volatile void *p) { (void)p; }

static Placed o[8192];
static int cw, ch, base;
static int shape(const char *s) { return font_shape((const unsigned char *)s, strlen(s), o, 8192); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static void show(const char *name, const char *s)
{
	int n = shape(s);
	printf("%-28s %d glyph(s):", name, n);
	for (int i = 0; i < n; i++) printf(" [cl %zu x %d y %d %dx%d %s]", o[i].cluster, o[i].x, o[i].y, o[i].g->w, o[i].g->h, o[i].g->color ? "colour" : "plain");
	printf("\n");
}

/* allocations made by shaping s when every glyph is already cached */
static void hit_allocs(const char *name, const char *warm, const char *s)
{
	unsigned long h0, m0, h1, m1;
	shape(warm);
	font_stats(&h0, &m0);
	allocs = bytes = 0;
	int n = shape(s);
	unsigned long a = allocs, b = bytes;
	font_stats(&h1, &m1);
	printf("%-34s glyphs %d misses %lu allocations %lu (%lu bytes)\n", name, n, m1 - m0, a, b);
}

static long rss_kb(void)
{
	long a = 0, b = 0;
	FILE *f = fopen("/proc/self/statm", "r");
	if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); }
	return b * 4;
}

int main(int argc, char **argv)
{
	long rss0 = rss_kb();
	const char *e = argc > 1 ? argv[1] : "";
	if (!getenv("FONTCONFIG_FILE")) font_env();
	const char *fam = getenv("FAMILY") ? getenv("FAMILY") : FAMILY;
	int px = getenv("PX") ? atoi(getenv("PX")) : 20;
	double t0 = now();
	if (font_open(fam, px)) { puts("font_open failed"); return 1; }
	double t1 = now();
	font_cell(&cw, &ch, &base);
	if (!strcmp(e, "open")) { printf("font_open %.2f ms; cell %dx%d base %d\n", t1 - t0, cw, ch, base); }
	if (!strcmp(e, "rss")) {
		long r1 = rss_kb();
		shape("theorem foo {\xce\xb1 : Type*} (f : \xce\xb1 \xe2\x86\x92 \xe2\x84\x9d) : \xe2\x88\x80 x, \xe2\x80\x96" "f x\xe2\x80\x96 \xe2\x89\xa4 \xce\xb5 \xf0\x9d\x93\x9d");
		long r2 = rss_kb();
		shape("\xe4\xb8\xad");
		long r3 = rss_kb();
		shape("\xf0\x9f\x98\x80");
		printf("RSS kB: before font_open %ld, after %ld (+%ld), after a Lean line %ld, after one CJK character %ld, after one emoji %ld\n", rss0, r1, r1 - rss0, r2, r3, rss_kb());
	}
	if (!strcmp(e, "wide") && argc > 2) {
		/* argv[2]: code points in hex, one per line (mathlib_symbols.py's output).
		   How many draw outside their own cell (wider, or starting left of it)? */
		FILE *f = fopen(argv[2], "r");
		unsigned v, total = 0, over = 0, over3 = 0;
		while (f && fscanf(f, "%x", &v) == 1) {
			char s[5] = { 0 };
			if (v < 0x800) { s[0] = (char)(0xC0 | v >> 6); s[1] = (char)(0x80 | (v & 63)); }
			else if (v < 0x10000) { s[0] = (char)(0xE0 | v >> 12); s[1] = (char)(0x80 | (v >> 6 & 63)); s[2] = (char)(0x80 | (v & 63)); }
			else { s[0] = (char)(0xF0 | v >> 18); s[1] = (char)(0x80 | (v >> 12 & 63)); s[2] = (char)(0x80 | (v >> 6 & 63)); s[3] = (char)(0x80 | (v & 63)); }
			int n = shape(s), l = 0, r = 0;
			for (int i = 0; i < n; i++) {
				int a = o[i].x + o[i].g->left, b = a + o[i].g->w;
				if (a < l) l = a;
				if (b > r) r = b;
			}
			total++;
			if (l < 0 || r > cw) over++;
			if (l < -3 || r > cw + 3) { over3++; printf(" U+%04X(%d..%d)", v, l, r); }
		}
		printf("\n%u symbols, cell %d px wide: %u have ink outside their cell, %u by more than 3 px (listed)\n", total, cw, over, over3);
	}
	if (!strcmp(e, "shapes")) {
		show("flag NL (2 RI)", "\xf0\x9f\x87\xb3\xf0\x9f\x87\xb1");
		show("keycap 1 FE0F 20E3", "1\xef\xb8\x8f\xe2\x83\xa3");
		show("heart FE0E (text)", "\xe2\x9d\xa4\xef\xb8\x8e");
		show("smile FE0E (text)", "\xf0\x9f\x98\x80\xef\xb8\x8e");
		show("e + acute", "e\xcc\x81");
		show("x + U+20D7 (vector)", "x\xe2\x83\x97");
		show("alpha + hat", "\xce\xb1\xcc\x82");
		show("N-script + hat", "\xf0\x9d\x93\x9d\xcc\x82");
		show("!= ligature", "!=");
		show("<=> ligature", "<=>");
		show("tab", "\t");
		show("NUL-less control 0x01", "\x01");
		show("PUA U+E0A0", "\xee\x82\xa0");
		show("arabic lam alef", "\xd9\x84\xd8\xa7");
		show("hangul jamo", "\xe1\x84\x80\xe1\x85\xa1");
		show("devanagari ksha", "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7");
		show("wide CJK", "\xe4\xb8\xad");
	}
	if (!strcmp(e, "allocs")) {
		if (!__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free)) return 2;
		hit_allocs("same line again", "abc def", "abc def");
		hit_allocs("longer run, glyphs cached", "abc", "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc");
		hit_allocs("Greek first, glyphs cached", "a \xce\xb1", "\xce\xb1 a");
		hit_allocs("Cyrillic first, glyphs cached", "a \xd0\xb6", "\xd0\xb6 a");
		hit_allocs("Greek first again", "a \xce\xb1", "\xce\xb1 a");
		hit_allocs("fallback glyph cached, alone", "a \xf0\x9d\x93\x9d", "\xf0\x9d\x93\x9d");
		hit_allocs("fallback glyph, again", "\xf0\x9d\x93\x9d", "\xf0\x9d\x93\x9d");
	}
	if (!strcmp(e, "nofont")) {
		/* characters no font has: how long a row of them takes, every time it is drawn */
		static char s[4 * 512];
		int reps = 200;
		const struct { const char *name; unsigned first; } k[] = {
			{ "ASCII letters", 'a' }, { "PUA U+E000.. (no font)", 0xE000 }, { "unassigned U+0378", 0x378 },
			{ "Mathlib's A7F4 (no font)", 0xA7F4 }, { "math script U+1D4D0.. (fallback)", 0x1D4D0 } };
		for (size_t c = 0; c < sizeof k / sizeof *k; c++) {
			size_t n = 0;
			for (int i = 0; i < 100; i++) {
				unsigned v = k[c].first + (k[c].first == 0x378 || k[c].first == 0xA7F4 ? 0 : (unsigned)(i % 26));
				if (v < 0x80) s[n++] = (char)v;
				else if (v < 0x800) { s[n++] = (char)(0xC0 | v >> 6); s[n++] = (char)(0x80 | (v & 63)); }
				else if (v < 0x10000) { s[n++] = (char)(0xE0 | v >> 12); s[n++] = (char)(0x80 | (v >> 6 & 63)); s[n++] = (char)(0x80 | (v & 63)); }
				else { s[n++] = (char)(0xF0 | v >> 18); s[n++] = (char)(0x80 | (v >> 12 & 63)); s[n++] = (char)(0x80 | (v >> 6 & 63)); s[n++] = (char)(0x80 | (v & 63)); }
			}
			s[n] = 0;
			shape(s);
			double a = now();
			for (int r = 0; r < reps; r++) shape(s);
			double b = now();
			printf("%-36s row of 100: %.1f us per row (all cache hits)\n", k[c].name, (b - a) * 1e3 / reps);
		}
	}
	if (!strcmp(e, "emoji")) {
		show("grinning face", "\xf0\x9f\x98\x80");
		show("family (ZWJ)", "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
		show("rainbow flag", "\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x8c\x88");
		show("thumbs up, medium", "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd");
		show("heart + FE0F", "\xe2\x9d\xa4\xef\xb8\x8f");
		show("N-script", "\xf0\x9d\x93\x9d");
	}
	if (!strcmp(e, "scripts")) {
		/* a run whose first letter is of a new script makes HarfBuzz build a
		   shape plan; does a second round over the same runs still allocate? */
		static const unsigned first[] = { 'a', 0x3B1, 0x436, 0x5D0, 0x627, 0x561, 0x10D0, 0xE01, 0x915, 0xAC00, 0x4E2D,
			0x3042, 0x30A2, 0x995, 0xB95, 0x1200, 0x13A0, 0x1780, 0xE81, 0xF40, 0x1000, 0xD85, 0xA95, 0xA15, 0xC15,
			0xC95, 0xD15, 0x710, 0x780, 0x7C0, 0x1820, 0x2D30 };
		char s[32][8];
		if (!__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free)) return 2;
		for (int round = 0; round < 3; round++) {
			unsigned long h0, m0, h1, m1;
			font_stats(&h0, &m0);
			allocs = 0;
			for (size_t i = 0; i < sizeof first / sizeof *first; i++) {
				unsigned v = first[i];
				size_t n = 0;
				if (v < 0x80) s[i][n++] = (char)v;
				else if (v < 0x800) { s[i][n++] = (char)(0xC0 | v >> 6); s[i][n++] = (char)(0x80 | (v & 63)); }
				else { s[i][n++] = (char)(0xE0 | v >> 12); s[i][n++] = (char)(0x80 | (v >> 6 & 63)); s[i][n++] = (char)(0x80 | (v & 63)); }
				s[i][n] = 0;
				shape(s[i]);
			}
			unsigned long a = allocs;
			font_stats(&h1, &m1);
			printf("round %d over 32 scripts: misses %lu allocations %lu\n", round, m1 - m0, a);
		}
	}
	if (!strcmp(e, "long")) {
		/* a run longer than RUN characters */
		static char s[5000];
		memset(s, 'a', 4999);
		int n = shape(s);
		printf("4999 letters: %d glyphs, last cluster %zu\n", n, o[n - 1].cluster);
	}
	font_close();
	return 0;
}
