/* libFuzzer: uni.h's cluster kernels on any bytes, against the code point
   model (uni_len): clusters tile the line, are whole code points, hold no
   ASCII byte after their first and split an invalid byte from what follows;
   uni_gprev from every byte finds the cluster that holds the byte before;
   widths are 1, 2, 4 or 6, 1 for ASCII and 4 for an invalid byte.  Neovim
   agreement is tests/test_uni_nvim.c's job. */
#include <stdint.h>
#include <stdlib.h>
#include "uni.h"

int LLVMFuzzerTestOneInput(const uint8_t *s, size_t n)
{
	static unsigned char start[257];
	if (n == 0 || n > 256) return 0;
	for (size_t i = 0; i <= n; i++) start[i] = 0;
	size_t i = 0;
	while (i < n) {
		size_t g = uni_glen(s, n, i), k = uni_len(s, n, i), j = i + k;
		int w = uni_width(s, n, i);
		if (g < 1 || g > n - i) abort();
		if (k == 1 && s[i] >= 0x80 && (g != 1 || w != 4)) abort();
		if (s[i] < 0x80 && w != 1) abort();
		if (w != 1 && w != 2 && w != 4 && w != 6) abort();
		while (j < i + g) {
			if (s[j] < 0x80) abort();
			j += uni_len(s, n, j);
		}
		if (j != i + g) abort();
		start[i] = 1;
		i += g;
	}
	for (i = 1; i <= n; i++) {
		size_t b = uni_gprev(s, n, i);
		if (!start[b] || b >= i || i > b + uni_glen(s, n, b)) abort();
	}
	return 0;
}
