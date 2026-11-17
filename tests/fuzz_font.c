/* libFuzzer: font_shape on arbitrary bytes.  Against a simple model of what
   the backend relies on: at most max glyphs; clusters in order, each at a
   character start as uni_len steps; positions near their own cluster; and
   the same run shaped again (from the cache) gives the same glyphs. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "test_font_env.h"
#include "font.h"
#include "uni.h"

static Placed a[1024], b[1024];
static int cw;

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!cw) {
		int h, base;
		font_env();
		if (font_open(FAMILY, 20)) abort();
		font_cell(&cw, &h, &base);
	}
	if (n < 2) return 0;
	int max = 1 + d[0] % 255;
	const unsigned char *s = d + 1;
	n--;
	int k = font_shape(s, n, a, max);
	if (k < 0 || k > max) abort();
	static unsigned char start[256];
	memset(start, 0, n);
	for (size_t i = 0; i < n; i += uni_len(s, n, i)) start[i] = 1;
	for (int i = 0; i < k; i++) {
		size_t c = a[i].cluster, next = n;
		if (c >= n || !start[c] || (i > 0 && c < a[i - 1].cluster)) abort();
		for (int j = i + 1; j < k; j++) if (a[j].cluster != c) { next = a[j].cluster; break; }
		long reach = 4L * cw * (long)(next - c + 1);
		if (a[i].x > reach || a[i].x < -reach) abort();
		const Glyph *g = a[i].g;
		if (!g || g->w < 0 || g->h < 0) abort();
		unsigned sum = 0;
		for (size_t p = 0; p < (size_t)g->w * (size_t)g->h * (g->color ? 4 : 1); p++) sum += g->px[p];
		if (g->w && !g->px) abort();
		(void)sum;
	}
	Glyph keep[1024];
	for (int i = 0; i < k; i++) keep[i] = *a[i].g;
	if (font_shape(s, n, b, max) != k) abort();
	for (int i = 0; i < k; i++)
		if (b[i].cluster != a[i].cluster || b[i].x != a[i].x || b[i].y != a[i].y || b[i].g->w != keep[i].w ||
		    b[i].g->h != keep[i].h || b[i].g->left != keep[i].left || b[i].g->top != keep[i].top) abort();
	return 0;
}
