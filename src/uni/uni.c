/* Unicode kernels; their contracts are in uni.h.  uni_len, uni_prev and
   uni_cp are ref/uni.c's, already proved.  Cluster stepping copies Neovim
   0.12: utfc_ptr2len() over utf8proc 2.12's grapheme_break_extended(). */
#include "uni.h"
#include "tab.h"

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

/* The code point of the k-byte sequence at i (k from uni_len), or the byte
   itself when k is 1: what Neovim's utf_ptr2char() reads, invalid bytes
   included, so that clusters break where Neovim's do. */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1)) && k == ulen(s, n, i);
    assigns \nothing;
    ensures \result == cpx(s, n, i);
    ensures \result < 0x200000;
*/
static unsigned dec(const unsigned char *s, size_t n, size_t i, size_t k)
{
	unsigned c = s[i];
	(void)n;  /* the contract reads the line through n */
	if (k == 1) return c;
	if (k == 2) return (c - 0xC0) * 64 + (s[i + 1] - 0x80u);
	if (k == 3) return ((c - 0xE0) * 64 + (s[i + 1] - 0x80u)) * 64 + (s[i + 2] - 0x80u);
	return (((c - 0xF0) * 64 + (s[i + 1] - 0x80u)) * 64 + (s[i + 2] - 0x80u)) * 64 + (s[i + 3] - 0x80u);
}

unsigned uni_cp(const unsigned char *s, size_t n, size_t i)
{
	size_t k = uni_len(s, n, i);
	return k == 1 && s[i] >= 0x80 ? 0xFFFD : dec(s, n, i, k);
}

/* The property byte of c (gen.py): gcb + 16 * incb + 64 * cellc.  That the
   tables hold the Unicode data is not a proof goal but a fact about data,
   checked on every c below 2^21 by tests/test_uni_nvim.c, so it is admitted;
   that the reads stay in bounds is proved (uni_t2 has 256 rows). */
/*@ assigns \nothing;
    admit ensures 0 <= gcb(c) <= 12 && 0 <= incb(c) <= 3 && 0 <= cellc(c) <= 3;
    admit ensures \result == gcb(c) + 16 * incb(c) + 64 * cellc(c);
*/
static unsigned prop(unsigned c)
{
	return c < 0x110000 ? uni_t2[uni_t1[c / UNI_B]][c % UNI_B] : 0;
}

size_t uni_glen(const unsigned char *s, size_t n, size_t i)
{
	/* Neovim's ASCII fast path: nothing joins ASCII before ASCII */
	if (s[i] < 0x80 && (i + 1 == n || s[i + 1] < 0x80)) return 1;
	size_t k = uni_len(s, n, i);
	if (k == 1 && s[i] >= 0x80) return 1;
	unsigned p = dec(s, n, i, k), q = prop(p);
	unsigned l = q % 16, ic = q / 16 % 4;
	size_t j = i + k;
	/*@ loop invariant i < j <= n;
	    loop invariant 0 <= l <= 13 && 0 <= ic <= 3;
	    loop invariant glen(s, n, i) == j - i + more(s, n, j, l, ic, p);
	    loop assigns j, k, p, q, l, ic;
	    loop variant n - j; */
	while (j < n && s[j] >= 0x80) {
		k = uni_len(s, n, j);
		unsigned c = dec(s, n, j, k);
		q = prop(c);
		unsigned t = q % 16, it = q / 16 % 4;
		//@ assert c == cpx(s, n, j) && k == ulen(s, n, j) && t == gcb(c) && it == incb(c);
		int glue = l != 1 && t != 1 &&
			((l == 3 && (t == 3 || t == 4 || t == 6 || t == 7)) ||
			 ((l == 6 || l == 4) && (t == 4 || t == 5)) ||
			 ((l == 7 || l == 5) && t == 5) ||
			 t == 2 || t == 11 || t == 9 || l == 10 ||
			 (l == 13 && t == 12) || (l == 8 && t == 8));
		/* GB3 (CR x LF) by code point: gcb has one class for CR, LF and
		   Control, and a cluster whose last code point is CR is CR alone */
		if (!glue && !(ic == 1 && it == 2) && !(p == 0x0D && c == 0x0A) &&
		    !(p == 0x644 && (c == 0x622 || c == 0x623 || c == 0x625 || c == 0x627)))
			break;
		/* the step that loop invariant 3 needs, as its own goal: proved
		   whole, it ran close to the 20 s limit (CI timed out) */
		//@ assert more(s, n, j, l, ic, p) == k + more(s, n, j + k, nbc(l, t), nic(ic, it), c);
		l = l == 8 && t == 8 ? 0 : l == 12 && t == 2 ? 12 : l == 12 && t == 11 ? 13 : t;
		ic = ic == 1 && it == 3 ? 1 : it;
		p = c;
		j += k;
	}
	return j - i;
}

/* Back to an ASCII byte or the line start, both cluster starts, then
   forward: the start found is one the forward steps make (bound).  The cost
   grows with the distance to that byte: 5.7 ms on a 1 MB line without ASCII
   (measured, PR 12 review); Neovim's and Helix's steps are bounded. */
size_t uni_gprev(const unsigned char *s, size_t n, size_t i)
{
	size_t j = i - 1;
	/*@ loop invariant 0 <= j < i;
	    loop assigns j;
	    loop variant j; */
	while (j > 0 && s[j] >= 0x80) j--;
	/*@ loop invariant 0 <= j < i && bound(s, n, j);
	    loop assigns j;
	    loop variant i - j; */
	for (;;) {
		size_t k = uni_glen(s, n, j);
		if (j + k >= i) return j;
		j += k;
	}
}

int uni_width(const unsigned char *s, size_t n, size_t i)
{
	if (s[i] < 0x80) return 1;
	size_t k = uni_len(s, n, i);
	if (k == 1) return 4;
	unsigned c = dec(s, n, i, k), q = prop(c), w = q / 64, g = q % 16;
	/* utf_ptr2cells(): an emoji that is narrow alone is wide before VS16
	   (its regional indicators are wide anyway, so they need no test) */
	if (w == 0 && g == 12 && i + k < n && dec(s, n, i + k, uni_len(s, n, i + k)) == 0xFE0F)
		return 2;
	return w == 0 ? 1 : w == 1 ? 2 : w == 2 ? 4 : 6;
}

int uni_put(unsigned char *b, unsigned c)
{
	if (c < 0x80) { b[0] = (unsigned char)c; return 1; }
	if (c < 0x800) { b[0] = (unsigned char)(0xC0 + c / 64); b[1] = (unsigned char)(0x80 + c % 64); return 2; }
	if (c < 0x10000) { b[0] = (unsigned char)(0xE0 + c / 4096); b[1] = (unsigned char)(0x80 + c / 64 % 64); b[2] = (unsigned char)(0x80 + c % 64); return 3; }
	b[0] = (unsigned char)(0xF0 + c / 262144); b[1] = (unsigned char)(0x80 + c / 4096 % 64); b[2] = (unsigned char)(0x80 + c / 64 % 64); b[3] = (unsigned char)(0x80 + c % 64);
	return 4;
}
