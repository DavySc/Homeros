/* Reference kernels; their contracts are in uni.h.  uni_len is the review's
   comparison form of the UTF-8 kernel (design/reviews/r2/fc/u8r_cmp.c). */
#include "uni.h"

size_t uni_len(const unsigned char *s, size_t n, size_t i)
{
	unsigned char c = s[i];
	size_t k = c < 0x80 ? 1 : c < 0xC0 ? 0 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF8 ? 4 : 0;
	//@ assert k == want(c);
	if (k == 0 || k > n - i) return 1;
	/*@ loop invariant 1 <= j <= k;
	    loop invariant \forall integer m; 1 <= m < j ==> cont(s[i + m]);
	    loop assigns j;
	    loop variant k - j; */
	for (size_t j = 1; j < k; j++)
		if (s[i + j] < 0x80 || s[i + j] >= 0xC0) return 1;
	return k;
}

/* The sequence of length 2, 3 or 4 that ends at i, if one does (at most one
   can); else one byte back.  Neovim's utf_head_off() finds the same start by
   walking back over continuation bytes; three comparisons need no loop. */
size_t uni_prev(const unsigned char *s, size_t n, size_t i)
{
	if (i >= 2 && uni_len(s, n, i - 2) == 2) return i - 2;
	if (i >= 3 && uni_len(s, n, i - 3) == 3) return i - 3;
	if (i >= 4 && uni_len(s, n, i - 4) == 4) return i - 4;
	return i - 1;
}

unsigned uni_cp(const unsigned char *s, size_t n, size_t i)
{
	size_t k = uni_len(s, n, i);
	unsigned c = s[i];
	/* one case per length, no loop, so the provers see each formula */
	if (k == 1) return c < 0x80 ? c : 0xFFFD;
	if (k == 2) return (c - 0xC0) * 64 + (s[i + 1] - 0x80u);
	if (k == 3) return ((c - 0xE0) * 64 + (s[i + 1] - 0x80u)) * 64 + (s[i + 2] - 0x80u);
	return (((c - 0xF0) * 64 + (s[i + 1] - 0x80u)) * 64 + (s[i + 2] - 0x80u)) * 64 + (s[i + 3] - 0x80u);
}

/* The reference steps by code point: a cluster is one code point, one cell. */
size_t uni_glen(const unsigned char *s, size_t n, size_t i) { return uni_len(s, n, i); }
size_t uni_gprev(const unsigned char *s, size_t n, size_t i) { return uni_prev(s, n, i); }
int uni_width(const unsigned char *s, size_t n, size_t i) { (void)s; (void)n; (void)i; return 1; }
