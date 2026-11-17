/* The UTF-16 boundary, proved by Frama-C WP against lsp.h's contracts.
   Written as comparisons, no bit tricks (CLAUDE.md), so the provers finish. */
#include "lsp.h"

/*@ requires i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures \result == u16len(s, n, i);
*/
static size_t step(const unsigned char *s, size_t n, size_t i)
{
	unsigned c = s[i];
	if (c >= 0xF0 && c < 0xF8 && n - i >= 4 && s[i + 1] >= 0x80 && s[i + 1] < 0xC0
	    && s[i + 2] >= 0x80 && s[i + 2] < 0xC0 && s[i + 3] >= 0x80 && s[i + 3] < 0xC0)
		return 4;
	if (c >= 0xE0 && c < 0xF0 && n - i >= 3 && s[i + 1] >= 0x80 && s[i + 1] < 0xC0
	    && s[i + 2] >= 0x80 && s[i + 2] < 0xC0)
		return 3;
	if (c >= 0xC0 && c < 0xE0 && n - i >= 2 && s[i + 1] >= 0x80 && s[i + 1] < 0xC0)
		return 2;
	return 1;
}

size_t lsp_u16(const unsigned char *s, size_t n)
{
	size_t i = 0, u = 0;
	/*@ loop invariant 0 <= i <= n && 0 <= u <= i && at(s, n, i, u);
	    loop assigns i, u;
	    loop variant n - i;
	*/
	while (i < n) {
		size_t k = step(s, n, i);
		u += k == 4 ? 2 : 1;
		i += k;
	}
	return u;
}

size_t lsp_u16_byte(const unsigned char *s, size_t n, size_t c)
{
	size_t i = 0, u = 0;
	/*@ loop invariant 0 <= i <= n && 0 <= u <= i && u <= c && at(s, n, i, u);
	    loop assigns i, u;
	    loop variant n - i;
	*/
	while (i < n) {
		size_t k = step(s, n, i), w = k == 4 ? 2 : 1;
		if (c < u + w) return i;
		u += w;
		i += k;
	}
	return i;
}
