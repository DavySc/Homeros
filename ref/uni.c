/* Reference kernels.  uni_len is the review's comparison form of the UTF-8
   kernel (reviews/r2/fc/u8r_cmp.c): its contract says what the result is,
   and Frama-C WP proves it (make frama). */
#include "uni.h"

/*@ predicate cont(integer c) = 0x80 <= c < 0xC0;
    logic integer want(integer c) = c < 0x80 ? 1 : (0xC0 <= c < 0xE0) ? 2 :
                                    (0xE0 <= c < 0xF0) ? 3 : (0xF0 <= c < 0xF8) ? 4 : 0;
    predicate wellformed{L}(unsigned char *s, integer n, integer i, integer k) =
      k == want(s[i]) && k >= 1 && i + k <= n &&
      \forall integer j; 1 <= j < k ==> cont(s[i + j]);
*/
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures 1 <= \result <= 4 && i + \result <= n;
    ensures wellformed(s, n, i, want(s[i])) ==> \result == want(s[i]);
    ensures !wellformed(s, n, i, want(s[i])) ==> \result == 1;
*/
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

/* Back over at most three continuation bytes; keep the step only if the
   sequence found there really ends at i (Neovim's utf_head_off rule). */
/*@ requires n > 0 && 0 < i <= n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures i - 4 <= \result < i;
    ensures \result == i - 1 || wellformed(s, n, \result, i - \result);
*/
size_t uni_prev(const unsigned char *s, size_t n, size_t i)
{
	size_t j = i - 1;
	/*@ loop invariant i - 4 <= j < i;
	    loop assigns j;
	    loop variant j; */
	while (j > 0 && i - j < 4 && s[j] >= 0x80 && s[j] < 0xC0) j--;
	return uni_len(s, n, j) == i - j ? j : i - 1;
}

unsigned uni_cp(const unsigned char *s, size_t n, size_t i)
{
	size_t k = uni_len(s, n, i);
	unsigned c = s[i];
	if (k == 1) return c < 0x80 ? c : 0xFFFD;
	c = k == 2 ? c - 0xC0 : k == 3 ? c - 0xE0 : c - 0xF0;
	for (size_t j = 1; j < k; j++) c = c * 64 + (s[i + j] - 0x80u);
	return c;
}

int uni_width(unsigned cp) { (void)cp; return 1; }
