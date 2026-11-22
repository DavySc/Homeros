/* PR 102 review: where a first load's time goes: stb_image's decode, then
   stb_image_resize's sRGB scale to the box (1280 x 792 here), as image.c
   calls them.  Usage: split IMG... */
#include <stdio.h>
#include <stdlib.h>
#include "file.h"
#include "loop.h"
#include "stb_image.h"
#include "stb_image_resize.h"

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		for (int run = 0; run < 5; run++) {
			char *d; size_t n; FileId id; int w, h, k;
			long long a = loop_now();
			if (file_load(argv[i], &d, &n, &id)) return 1;
			unsigned char *p = stbi_load_from_memory((unsigned char *)d, (int)n, &w, &h, &k, 4);
			long long b = loop_now();
			int W = 1280, H = (int)((long long)h * W / w);
			if (H > 792) { H = 792; W = (int)((long long)w * H / h); }
			unsigned char *q = malloc((size_t)W * H * 4);
			stbir_resize_uint8_srgb(p, w, h, 0, q, W, H, 0, 4, 3, 0);
			long long c = loop_now();
			printf("%s: %dx%d decode %.1f ms, resize to %dx%d %.1f ms\n", argv[i], w, h, (b - a) / 1e6, W, H, (c - b) / 1e6);
			free(d); free(p); free(q);
		}
	return 0;
}
