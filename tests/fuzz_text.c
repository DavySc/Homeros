/* libFuzzer: text.h against an array model, ops decoded from the input. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char m[1 << 14], b[1 << 14];
	size_t mn = 0;
	Text *t = text_new("", 0);
	if (!t) abort();
	for (size_t i = 0; i + 3 <= n; i += 3) {
		size_t pos = mn ? d[i + 1] % (mn + 1) : 0, k = d[i + 2] % 9;
		if (d[i] & 1) {
			if (mn + k > sizeof m) continue;
			char s[9];
			for (size_t j = 0; j < k; j++) s[j] = "ab\n\xc3\xa9xyz\n"[(d[i] + j) % 9];
			if (text_insert(t, pos, s, k)) abort();
			memmove(m + pos + k, m + pos, mn - pos); memcpy(m + pos, s, k); mn += k;
		} else {
			if (k > mn - pos) k = mn - pos;
			if (text_delete(t, pos, k)) abort();
			memmove(m + pos, m + pos + k, mn - pos - k); mn -= k;
		}
		if (text_len(t) != mn || text_read(t, 0, b, sizeof b) != mn || memcmp(b, m, mn)) abort();
		size_t l = text_line_of(t, pos), ls = text_line_start(t, l);
		if (ls > pos || memchr(m + ls, '\n', pos - ls)) abort();
	}
	text_free(t);
	return 0;
}
