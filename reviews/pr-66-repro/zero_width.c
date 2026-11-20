#include <stdio.h>
#include <string.h>
#include "grid.h"
#include "uni.h"
int main(void)
{
	Grid g = { 0 }; grid_resize(&g, 1, 20);
	const char *t[] = { "\xcc\x81" "abc", "a\xcc\x81" "bc", "\xe2\x80\x8d" "x", "\xef\xbb\xbf" "x", "\xc2\xad" "x", "a\x01" "b", "a\tb", "\xf3\xa0\x80\x81x" };
	for (unsigned k = 0; k < sizeof t / sizeof *t; k++) {
		grid_clear(&g);
		size_t n = strlen(t[k]);
		int e = grid_puts(&g, 0, 0, g.cols, t[k], n, ATTR_NONE, FACE_NONE);
		printf("bytes"); for (size_t i = 0; i < n; i++) printf(" %02x", (unsigned char)t[k][i]);
		printf(": first width %d, grid_width %d, grid_puts -> %d, cells:", uni_width((const unsigned char *)t[k], n, 0), grid_width(t[k], n), e);
		for (int c = 0; c < 6; c++) { printf(" ["); fwrite(grid_text(&g.c[c]), 1, g.c[c].len > 4 ? 8 : g.c[c].len, stdout); printf("]"); }
		puts("");
	}
	grid_free(&g);
}
