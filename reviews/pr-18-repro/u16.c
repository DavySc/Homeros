/* lsp.h says of its UTF-16 contract: "Bytes step as uni_len".  Does its
   logic function u16len (transcribed below, line for line) give the same
   length as uni_len?  Every 1- to 4-byte window with a lead byte >= 0x80 is
   tried (continuation bytes from a set that covers each range boundary).
     gcc -Iinclude -o /tmp/u16 reviews/pr-18-repro/u16.c ref/uni.c && /tmp/u16 */
#include <stdio.h>
#include "uni.h"

static int cont(int c) { return 0x80 <= c && c < 0xC0; }
static size_t u16len(const unsigned char *s, size_t n, size_t i)
{
	return (0xF0 <= s[i] && s[i] < 0xF8 && i + 4 <= n && cont(s[i + 1]) && cont(s[i + 2]) && cont(s[i + 3])) ? 4 :
	       (0xE0 <= s[i] && s[i] < 0xF0 && i + 3 <= n && cont(s[i + 1]) && cont(s[i + 2])) ? 3 :
	       (0xC0 <= s[i] && s[i] < 0xE0 && i + 2 <= n && cont(s[i + 1])) ? 2 : 1;
}

int main(void)
{
	static const unsigned char tail[] = { 0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xFF };
	enum { T = sizeof tail };
	long tried = 0, differ = 0; int shown = 0;
	for (int lead = 0x80; lead < 0x100; lead++)
		for (int a = 0; a < T; a++) for (int b = 0; b < T; b++) for (int c = 0; c < T; c++) {
			unsigned char s[4] = { (unsigned char)lead, tail[a], tail[b], tail[c] };
			size_t u = uni_len(s, 4, 0), l = u16len(s, 4, 0);
			tried++;
			if (u == l) continue;
			differ++;
			if (shown++ < 8) printf("%02X %02X %02X %02X: uni_len %zu, lsp.h's u16len %zu\n", s[0], s[1], s[2], s[3], u, l);
		}
	printf("%ld windows, %ld differ\n", tried, differ);
	return differ != 0;
}
