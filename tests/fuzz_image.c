/* libFuzzer: the image cache on arbitrary input.  Odd first byte: the rest
   as a file's bytes (whatever decodes must fit its box).  Even: a PNG of
   the given pixels (stb_image_write), placed on a grid and drawn, against a
   model: cells inside the image and the grid hold their own tile, others
   are untouched, and an image that fits shows its pixels over bg. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static Grid g;
static uint32_t px[16 * 8 * 16 * 8];

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static unsigned k;
	char path[64];
	if (n < 12) return 0;
	snprintf(path, sizeof path, "/tmp/fuzz_image_%d_%u.png", (int)getpid(), k++);   /* a new name: no stale mtime */
	int w = 1 + d[1] % 8, h = 1 + d[2] % 8, cw = 1 + d[3] % 8, ch = 1 + d[4] % 8, bc = 1 + d[5] % 6, br = 1 + d[6] % 6;
	int rows = 1 + d[7] % 16, cols = 1 + d[8] % 16, r = d[9] % 20 - 2, c = d[10] % 20 - 2, skip = d[11] % 8 - 1;
	uint32_t bg = d[0] * 0x010203u & 0xffffff;
	const unsigned char *p = d + 12;
	n -= 12;
	FILE *f;
	if (d[0] & 1) {
		if (!(f = fopen(path, "wb")) || fwrite(p, 1, n, f) != n) abort();
		fclose(f);
	} else {
		unsigned char rgba[8 * 8 * 4];
		for (int i = 0; i < w * h * 4; i++) rgba[i] = (size_t)i < n ? p[i] : 0;
		if (!stbi_write_png(path, w, h, 4, rgba, w * 4)) abort();
	}
	int m = image_load(path, bc, br, cw, ch, bg), tc, tr;
	unlink(path);
	image_size(m, &tc, &tr);
	if (!m) return 0;
	if (tc < 1 || tr < 1 || tc > bc || tr > br) abort();
	if (grid_resize(&g, rows, cols)) abort();
	image_put(&g, r, c, m, skip);
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < cols; x++) {
			const Cell *e = &g.c[y * cols + x];
			int ty = y - r + skip, tx = x - c, in = ty >= (skip > 0 ? skip : 0) && ty < tr && tx >= 0 && tx < tc;
			if (!in) { if (e->attr != ATTR_NONE || e->len != 1 || e->ch[0] != ' ') abort(); continue; }
			if (e->attr != ATTR_IMAGE || e->len != 4 || e->ch[0] != m || e->ch[1] != tx || e->ch[2] != ty) abort();
			for (int i = 0; i < cw * ch; i++) px[i] = 1;
			image_draw(px, cw, e);
			for (int yy = 0; yy < ch; yy++)
				for (int xx = 0; xx < cw; xx++) {
					uint32_t v = px[yy * cw + xx];
					int ix = tx * cw + xx, iy = ty * ch + yy;
					if (v >> 24 != 0xff) abort();   /* drawn, opaque in an ARGB buffer too */
					if (d[0] & 1 || w > bc * cw || h > br * ch) continue;
					if (ix >= w || iy >= h) { if (v != (0xff000000 | bg)) abort(); continue; }
					size_t q = (size_t)(iy * w + ix) * 4;
					unsigned a = q + 3 < n ? p[q + 3] : 0;
					for (int s = 0; s < 3; s++) {
						unsigned cc = q + (size_t)s < n ? p[q + (size_t)s] : 0, b = bg >> (16 - 8 * s) & 0xff;
						if ((v >> (16 - 8 * s) & 0xff) != (cc * a + b * (255 - a) + 127) / 255) abort();
					}
				}
		}
	return 0;
}
