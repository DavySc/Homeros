/* font.h on the pinned font set (test_font_env.h): every non-ASCII
   character of Mathlib's notation draws; combining marks and ZWJ emoji are
   shaped; -> is a ligature; positions are per cluster; a second pass over
   the same text hits the cache and allocates nothing; the cache empties
   itself when full and draws the same glyphs again. */
#include <string.h>
#include "check.h"
#include "test_font_env.h"
#include "font.h"

/* ASan's hook sees every allocation in the process, the libraries' too. */
int __sanitizer_install_malloc_and_free_hooks(void (*)(const volatile void *, size_t), void (*)(const volatile void *));
static unsigned long allocs;
static void on_malloc(const volatile void *p, size_t n) { (void)p; (void)n; allocs++; }
static void on_free(const volatile void *p) { (void)p; }

/* Every non-ASCII character in Mathlib's notation: in its files named
   *otation*.lean and in its notation, infix, prefix, postfix, syntax and
   macro declarations, from src/font/mathlib_symbols.py on mathlib4 at
   7668accf6b41 (2026-11-17): 295 characters. */
static const char mathlib[] =
	"«¬²·¹»×äéëöĒʳʷʸˡˢˣ̂ΓΔΘΠΣΦΩαβγδεζηθ"
	"ικλμνπρστφχψωᗮᘁᴬᴴᴸᴹᵀᵃᵇᵈᵍᵐᵒᵖ"
	"ᵛᵢᵣᵤᵥᶜᶠ–‖†•…′‼⁄⁅⁆⁰ⁱ⁺⁻ⁿ₀"
	"₁₂₃₄₊ₐₑₒₓₕₖₗₘₙₚₛₜℂℋℍℓℕ℘"
	"ℙℚℝℤℬℵℶ⅟←↑→↓↔↘↥↦↧↪↾↿⇒⇔⇨"
	"∀∂∃∅∇∈∉∏∐∑∗∘∙√∞∠∡∣∤∧∨∩∪"
	"∫∮∯≀≃≅≌≠≡≤≥≪≫≺≼⊂⊃⊆⊇⊑⊓⊔⊕"
	"⊗⊙⊚⊛⊞⊠⊡⊢⊣⊤⊥⊨⊴⊵⊻⊼⋀⋂⋃⋆⋈⋊⋔"
	"⋖⋙⋯⌈⌉⌊⌋□△▷▸◁○◫♭♯✶⟂⟦⟧⟨⟩⟪"
	"⟫⟮⟯⟶⟹⤞⤳⥤⦃⦄⦋⦌⧸⨁⨂⨅⨆⨍⨯⨿⩿⬝⸨"
	"⸩／＼￢𝐞𝐤𝑭𝑮𝑺𝒄𝒑𝒜𝒢𝒪𝒫𝒬𝒮𝒰"
	"𝒱𝓀𝓂𝓐𝓕𝓘𝓚𝓞𝓟𝓡𝔖𝔠𝔪𝔭𝔸𝔹𝔻"
	"𝔼𝔽𝕀𝕄𝕊𝕋𝕌𝕎𝕖𝕜𝕝𝕢𝖣𝟙𝟭"
;

static Placed o[4096];
static int cw, ch, base;

static int shape(const char *s) { return font_shape((const unsigned char *)s, strlen(s), o, 4096); }

/* What the n glyphs in o draw, in one number. */
static unsigned long long hash(int n)
{
	unsigned long long h = 14695981039346656037ULL;
	for (int i = 0; i < n; i++) {
		const Glyph *g = o[i].g;
		int v[] = { (int)o[i].cluster, o[i].x, o[i].y, g->w, g->h, g->left, g->top, g->color };
		for (int j = 0; j < 8; j++) h = (h ^ (unsigned)v[j]) * 1099511628211ULL;
		for (size_t p = 0; p < (size_t)g->w * (size_t)g->h * (g->color ? 4 : 1); p++) h = (h ^ g->px[p]) * 1099511628211ULL;
	}
	return h;
}

static int ink(const Glyph *g)
{
	size_t n = (size_t)g->w * (size_t)g->h * (g->color ? 4 : 1), sum = 0;
	for (size_t i = 0; i < n; i++) sum += g->px[i];
	return sum > 0;
}

/* UTF-8 for v at s; returns its length. */
static int put(char *s, unsigned v)
{
	if (v < 0x10000) {
		s[0] = (char)(0xE0 | v >> 12); s[1] = (char)(0x80 | (v >> 6 & 63)); s[2] = (char)(0x80 | (v & 63));
		return 3;
	}
	s[0] = (char)(0xF0 | v >> 18); s[1] = (char)(0x80 | (v >> 12 & 63));
	s[2] = (char)(0x80 | (v >> 6 & 63)); s[3] = (char)(0x80 | (v & 63));
	return 4;
}

/* Of glyph s (one character): how many runs of ink row y has, or column
   x if y < 0; or, if both are -1, -v if every pixel is v (else 0) */
static int runs(const char *s, int x, int y)
{
	CHECK(shape(s) == 1);
	const Glyph *g = o[0].g;
	int k = 0, was = 0;
	if (y < 0 && x < 0) {
		for (int i = 0; i < g->w * g->h; i++) k |= g->px[i] != g->px[0];
		return k ? 0 : -g->px[0];
	}
	for (int i = 0, n = y < 0 ? g->h : g->w; i < n; i++) {
		int v = y < 0 ? g->px[i * g->w + x] : g->px[y * g->w + i];
		k += v && !was;
		was = v != 0;
	}
	return k;
}

/* From a fresh cache, shapes the characters first to last, 32 to a run:
   each draws; the cache was emptied on the way (the first character is a
   miss again) and draws the first character as before. */
static void refill(int px, unsigned first, unsigned last)
{
	char s[4 * 32 + 1];
	unsigned long hit, m0, m1;
	CHECK(font_open(FAMILY, px) == 0);
	s[put(s, first)] = 0;
	unsigned long long h0 = hash(shape(s));
	for (unsigned c = first; c < last; c += 32) {
		int k = 0;
		for (unsigned v = c; v < c + 32 && v < last; v++) k += put(s + k, v);
		s[k] = 0;
		int n = shape(s);
		for (int j = 0; j < n; j++) CHECK(ink(o[j].g));
	}
	font_stats(&hit, &m0);
	s[put(s, first)] = 0;
	CHECK(hash(shape(s)) == h0);
	font_stats(&hit, &m1);
	CHECK(m1 == m0 + 1);
}

int main(void)
{
	font_env();
	CHECK(font_open(FAMILY, 0) == -1);
	CHECK(font_open(FAMILY, 20) == 0);
	font_cell(&cw, &ch, &base);
	CHECK(cw > 0 && ch > cw && base > 0 && base < ch);

	/* U+0378 is unassigned: no font has it, so this is the first font's .notdef */
	CHECK(shape("\xcd\xb8") == 1);
	const Glyph *notdef = o[0].g;

	/* the gate: each symbol alone and after a letter, no .notdef, some ink;
	   and the same glyphs again from a fresh cache filled in reverse order
	   (a cache that mixed up fonts would hand out another font's glyph) */
	static unsigned long long sig[295];
	int nsym = 0, n;
	for (size_t i = 0; mathlib[i]; nsym++) {
		size_t k = 1;
		while ((mathlib[i + k] & 0xC0) == 0x80) k++;
		char s[8] = "x";
		memcpy(s + 1, mathlib + i, k);
		s[k + 1] = 0;
		for (int alone = 0; alone < 2; alone++) {
			int drawn = 0;
			n = shape(s + alone);
			CHECK(n > 0 && nsym < 295);
			for (int j = 0; j < n; j++) {
				if (o[j].g == notdef) fprintf(stderr, "no glyph for %s\n", s + 1);
				CHECK(o[j].g != notdef);
				drawn |= (o[j].cluster == (alone ? 0 : 1) || mathlib[i] == '\xcc') && ink(o[j].g);
			}
			CHECK(drawn);
		}
		sig[nsym] = hash(n);
		i += k;
	}
	CHECK(font_open(FAMILY, 20) == 0);
	for (int i = nsym - 1, end = (int)sizeof mathlib - 1; i >= 0; i--) {
		int start = end - 1;
		while ((mathlib[start] & 0xC0) == 0x80) start--;
		char s[8] = { 0 };
		memcpy(s, mathlib + start, (size_t)(end - start));
		CHECK(hash(shape(s)) == sig[i]);
		end = start;
	}
	CHECK(nsym == 295);

	/* q + U+0303: no precomposed glyph, so two glyphs in one cluster, the
	   tilde placed over the q */
	CHECK(shape("q\xcc\x83") == 2);
	CHECK(o[0].cluster == 0 && o[1].cluster == 0 && o[0].x == 0);
	int q0 = o[0].x + o[0].g->left, q1 = q0 + o[0].g->w, t0 = o[1].x + o[1].g->left, t1 = t0 + o[1].g->w;
	CHECK(t0 < q1 && q0 < t1 && o[1].g->top - o[1].g->h >= o[0].g->top - 1);
	/* a in a cluster with two marks */
	CHECK(shape("a\xcc\x81\xcc\xa3" "b") >= 2 && o[0].cluster == 0 && o[shape("a\xcc\x81\xcc\xa3" "b") - 1].cluster == 5);

	/* ZWJ sequences and a skin tone: one colour glyph each, about two cells */
	const char *emoji[] = {
		"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",   /* family */
		"\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x8c\x88",                  /* rainbow flag */
		"\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",                                           /* thumbs up, medium */
	};
	for (int i = 0; i < 3; i++) {
		CHECK(shape(emoji[i]) == 1);
		CHECK(o[0].cluster == 0 && o[0].g->color && ink(o[0].g));
		CHECK(o[0].g->w <= 2 * cw + 2 && o[0].g->h <= ch && o[0].g->w >= cw);
	}

	/* emoji presentation: in colour though DejaVu Sans, earlier in the
	   order, has a plain 😀; ❤ is text by default and colour with U+FE0F */
	CHECK(shape("\xf0\x9f\x98\x80") == 1 && o[0].g->color);
	n = shape("\xe2\x9d\xa4\xef\xb8\x8f");
	CHECK(n >= 1 && o[0].g->color);
	for (int j = 0; j < n; j++) CHECK(o[j].cluster == 0);
	CHECK(shape("\xe2\x9d\xa4") == 1 && !o[0].g->color);

	/* -> is a ligature: neither glyph is the one - or > gets alone */
	shape("-");
	const Glyph *minus = o[0].g;
	shape(">");
	const Glyph *gt = o[0].g;
	CHECK(shape("->") == 2 && o[0].g != minus && o[1].g != gt);

	/* positions are from each cluster's own cell */
	CHECK(shape("abc") == 3);
	for (int j = 0; j < 3; j++) CHECK(o[j].cluster == (size_t)j && o[j].x == 0 && o[j].y == 0);
	/* invalid UTF-8: one cluster per byte, as uni_len steps */
	CHECK(shape("\xff\xe2\x82" "a") == 4);
	for (int j = 0; j < 4; j++) CHECK(o[j].cluster == (size_t)j);
	CHECK(font_shape((const unsigned char *)"abc", 3, o, 2) == 2);
	/* clusters are uni.h's characters, overlong forms too (HarfBuzz's own
	   decoder would split C0 B2) */
	CHECK(shape("\xc0\xb2" "a") == 2 && o[0].cluster == 0 && o[1].cluster == 2);
	/* right-to-left text stays in logical order, as Neovim draws it */
	CHECK(shape("\xd7\x90\xd7\x91") == 2 && o[0].cluster == 0 && o[1].cluster == 2);

	/* a second pass over text already drawn hits the cache and allocates nothing */
	unsigned long h0, m0, h1, m1;
	const char *line = "theorem foo {α : Type*} [TopologicalSpace α] (f : α → ℝ) : ∀ᶠ x in 𝓝 a, ‖f x‖ ≤ ε ⦃ᵢ⦄ -> q\xcc\x83 \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd";
	shape(line);
	font_stats(&h0, &m0);
	CHECK(__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free));
	n = shape(line);
	unsigned long a = allocs;
	font_stats(&h1, &m1);
	CHECK(a == 0 && m1 == m0 && h1 == h0 + (unsigned long)n);
	/* and so does a run longer than any before, of glyphs already drawn
	   (HarfBuzz's buffer is taken at font_open, not grown here) */
	static char longer[2001];
	memset(longer, 'a', 2000);
	n = shape(longer);
	CHECK(allocs == 0 && n == 2000);

	/* box drawing, blocks and Powerline's separators are drawn to fill the
	   cell (sprite.c), not taken from JetBrains Mono, so neighbours join */
	for (unsigned c = 0x2500; c <= 0xE0B3; c = c == 0x259F ? 0xE0B0 : c + 1) {
		char s[4];
		s[put(s, c)] = 0;
		CHECK(shape(s) == 1 && o[0].x == 0 && o[0].y == 0 && ink(o[0].g));
		CHECK(o[0].g->w == cw && o[0].g->h == ch && o[0].g->left == 0 && o[0].g->top == base && !o[0].g->color);
	}
	CHECK(runs("\u2588", -1, -1) == -255 && runs("\u2592", -1, -1) == -128);   /* █ every pixel, ▒ half */
	CHECK(runs("\u2580", -1, 0) == 1 && runs("\u2580", -1, ch - 1) == 0 && runs("\u2580", cw - 1, -1) == 1);   /* ▀ */
	CHECK(runs("\u259a", -1, 0) == 1 && runs("\u259a", 0, -1) == 1 && runs("\u259a", -1, ch / 2 - 1) == 1);   /* ▚ */
	CHECK(o[0].g->px[0] == 255 && o[0].g->px[cw - 1] == 0);   /* its upper left, not its upper right */
	CHECK(runs("\u2500", 0, -1) == 1 && runs("\u2500", cw - 1, -1) == 1 && runs("\u2500", -1, 0) == 0);   /* ─ meets both sides */
	CHECK(runs("\u2502", -1, 0) == 1 && runs("\u2502", -1, ch - 1) == 1 && runs("\u2502", 0, -1) == 0);    /* │ top and bottom */
	CHECK(runs("\u253c", -1, 0) == 1 && runs("\u253c", -1, ch - 1) == 1 && runs("\u253c", 0, -1) == 1 && runs("\u253c", cw - 1, -1) == 1);
	CHECK(runs("\u2554", cw - 1, -1) == 2 && runs("\u2554", -1, ch - 1) == 2 && runs("\u2554", 0, -1) == 0);   /* ╔: two lines out right and down */
	CHECK(runs("\u256c", cw - 1, -1) == 2 && runs("\u256c", -1, 0) == 2 && runs("\u256c", 0, -1) == 2 && runs("\u256c", -1, ch / 2) == 0);  /* ╬: four corners, a gap between */
	CHECK(runs("\u255f", cw - 1, -1) == 1 && runs("\u255f", -1, ch / 2) == 2);   /* ╟: the arm meets the right line only */
	CHECK(runs("\u256d", cw - 1, -1) == 1 && runs("\u256d", -1, ch - 1) == 1 && runs("\u256d", -1, 0) == 0);   /* ╭ */

	/* the cache fills and empties itself, and glyphs stay right across it:
	   its slots with 8,000 CJK characters, its memory with every emoji at
	   60 pixels (about 30 MB) */
	refill(20, 0x4E00, 0x4E00 + 8000);
	refill(60, 0x1F300, 0x1FB00);
	font_close();
	printf("test_font: %d of %d Mathlib notation symbols draw; marks, ZWJ emoji, ligature, cache and refill\n", nsym, nsym);
	return 0;
}
