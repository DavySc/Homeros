/* Review of PR 12: time to step a text by cluster, uni_glen against
   Neovim's utfc_ptr2len() rewritten here over the real libutf8proc 2.12
   (utf_ptr2len/utf_ptr2char reduced to what valid UTF-8 needs).
   usage: speed uni|nvim   (prints clusters and ms for 5 passes over 8 MB) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utf8proc.h>
#include "uni.h"
static int len8(const unsigned char *p) { return *p < 0x80 ? 1 : *p < 0xE0 ? 2 : *p < 0xF0 ? 3 : 4; }
static int chr(const unsigned char *p)
{
	if (*p < 0x80) return *p;
	if (*p < 0xE0) return (p[0] - 0xC0) * 64 + p[1] - 0x80;
	if (*p < 0xF0) return ((p[0] - 0xE0) * 64 + p[1] - 0x80) * 64 + p[2] - 0x80;
	return (((p[0] - 0xF0) * 64 + p[1] - 0x80) * 64 + p[2] - 0x80) * 64 + p[3] - 0x80;
}
static int composinglike(const unsigned char *p1, const unsigned char *p2, utf8proc_int32_t *state)
{
	if (*p2 < 0x80) return 0;
	int a = chr(p1), b = chr(p2);
	if (!utf8proc_grapheme_break_stateful(a, b, state)) return 1;
	return a == 0x644 && (b == 0x622 || b == 0x623 || b == 0x625 || b == 0x627);
}
/* not inlined: Neovim calls it across files, as uni_glen is called here */
__attribute__((noinline)) static size_t nvim_glen(const unsigned char *p)   /* utfc_ptr2len(), mbyte.c */
{
	if (p[0] < 0x80 && p[1] < 0x80) return 1;
	int len = len8(p), prevlen = 0;
	utf8proc_int32_t state = 0;
	for (;;) {
		if (p[len] < 0x80 || !composinglike(p + prevlen, p + len, &state)) return (size_t)len;
		prevlen = len;
		len += len8(p + len);
	}
}
int main(int argc, char **argv)
{
	size_t n = 8u << 20, m = 0;
	unsigned char *s = malloc(n + 8);
	static const char *line[] = { "theorem foo (α : Type) (f : ℕ → ℕ) : ∀ x, f x ≤ f (x + 1) := by\n",
		"  intro x; simp [Nat.succ_le_iff] at h ⊢\n", "  exact ⟨fun h => h.1, fun h => ⟨h, rfl⟩⟩\n",
		"-- naïve café: e\xcc\x81 👨‍👩‍👧 🇳🇱 中文\n", "def bar := List.map (· + 1) [1, 2, 3]\n" };
	for (int k = 0; m + 100 < n; k++) { size_t l = strlen(line[k % 5]); memcpy(s + m, line[k % 5], l); m += l; }
	s[m] = 0;
	int uni = argc > 1 && !strcmp(argv[1], "uni");
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	size_t c = 0;
	for (int pass = 0; pass < 5; pass++)
		for (size_t i = 0; i < m; c++) i += uni ? uni_glen(s, m, i) : nvim_glen(s + i);
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("%s %zu clusters %.0f ms\n", uni ? "uni " : "nvim", c, (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6);
	return 0;
}
