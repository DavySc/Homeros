/* PR 102 review: the README's decoder comparison, PNG side: stb_image
   against libpng 1.6 (its simplified API), both from memory to RGBA, 5
   alternating runs of 20 decodes, the median of each run.
   Build: cc -O2 -Iinclude -I/usr/include/stb dec.c src/file/*.c src/loop/*.c src/text/*.c build/image/stb.o -lpng -lm
   Usage: dec FILE.png */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file.h"
#include "stb_image.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	char *d; size_t n; FileId id;
	if (argc < 2 || file_load(argv[1], &d, &n, &id)) return 1;
	for (int run = 0; run < 5; run++)
		for (int lib = 0; lib < 2; lib++) {
			double t[20];
			for (int i = 0; i < 20; i++) {
				double a = now();
				if (lib) {
					png_image im;
					memset(&im, 0, sizeof im);
					im.version = PNG_IMAGE_VERSION;
					if (!png_image_begin_read_from_memory(&im, d, n)) return 1;
					im.format = PNG_FORMAT_RGBA;
					unsigned char *p = malloc(PNG_IMAGE_SIZE(im));
					if (!png_image_finish_read(&im, NULL, p, 0, NULL)) return 1;
					free(p);
				} else {
					int w, h, k;
					unsigned char *p = stbi_load_from_memory((unsigned char *)d, (int)n, &w, &h, &k, 4);
					if (!p) return 1;
					stbi_image_free(p);
				}
				t[i] = now() - a;
			}
			qsort(t, 20, sizeof *t, cmp);
			printf("%s %.2f ms%s", lib ? "libpng" : "stb", t[10], lib ? "\n" : "   ");
		}
	return 0;
}
