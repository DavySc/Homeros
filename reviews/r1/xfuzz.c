/* One fuzzer for both builds: text.h against an array, shape after each op. */
#include "shape.h"
enum { MAX = 1 << 21 };
static char m[MAX], b[MAX], s[1 << 18];
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	size_t mn = 0;
	Text *t = text_new("", 0);
	if (!t) abort();
	for (size_t i = 0; i + 4 <= n; i += 4) {
		size_t pos = (size_t)(d[i + 1] << 8 | d[i + 2]) * (mn + 1) >> 16;
		unsigned z = d[i + 3];
		size_t k = z < 128 ? z % 16 : z < 224 ? (size_t)(z - 127) * 61 : (size_t)(z - 223) * 4099;
		switch (d[i] % 8) {
		case 0: case 1: case 2: case 3:
			if (mn + k > MAX) break;
			for (size_t j = 0; j < k; j++) s[j] = "ab\n\xc3\xa9xyz\n\n"[(d[i] + j * 7 + j / 13) % 10];
			if (text_insert(t, pos, s, k)) abort();
			memmove(m + pos + k, m + pos, mn - pos); memcpy(m + pos, s, k); mn += k;
			break;
		case 4: case 5: case 6:
			if (k > mn - pos) k = mn - pos;
			if (text_delete(t, pos, k)) abort();
			memmove(m + pos, m + pos + k, mn - pos - k); mn -= k;
			break;
		case 7:
			if (!text_insert(t, mn + 1 + k, s, 1) || !text_delete(t, pos, mn - pos + 1) || !text_delete(t, mn + 1, 0)) abort();
			if (z == 255) { if (text_delete(t, 0, mn)) abort(); mn = 0; }
			break;
		}
		if (pos > mn) pos = mn;
		shape(t);
		if (text_len(t) != mn || text_read(t, 0, b, MAX) != mn || memcmp(b, m, mn)) abort();
		size_t nl = 0, lines = 0;
		for (size_t j = 0; j < mn; j++) { if (j == pos) nl = lines; lines += m[j] == '\n'; }
		if (pos >= mn) nl = lines;
		if (text_lines(t) != lines || text_line_of(t, pos) != nl) abort();
		size_t ls = text_line_start(t, nl);
		if (ls > pos || (nl && m[ls - 1] != '\n') || (nl == 0 && ls) || memchr(m + ls, '\n', pos - ls)) abort();
		if (text_line_start(t, lines + 1) != mn || text_line_of(t, mn + 5) != lines) abort();
		size_t p = pos * 7 % (mn + 2), want = p < mn ? (k * 3 + 1 < mn - p ? k * 3 + 1 : mn - p) : 0;
		if (text_read(t, p, b, k * 3 + 1) != want || memcmp(b, m + p, want)) abort();
	}
	text_free(t);
	return 0;
}
