/* PR 102 review: what image_load costs on a hit (the core asks for every
   image on every frame), against the stat it makes, and what the first load
   of an image costs (on the loop).  Usage: lookup IMG [more images...] */
#include <stdio.h>
#include <sys/stat.h>
#include "image.h"
#include "loop.h"

int main(int argc, char **argv)
{
	enum { N = 200000 };
	struct stat st;
	for (int i = 1; i < argc; i++) {
		long long a = loop_now();
		int n = image_load(argv[i], 128, 36, 10, 22, 0x1e1e2e), c, r;
		image_size(n, &c, &r);
		printf("%s: first load %.1f ms, %dx%d cells\n", argv[i], (loop_now() - a) / 1e6, c, r);
	}
	for (int run = 0; run < 5; run++) {
		long long a = loop_now();
		for (int i = 0; i < N; i++) if (!image_load(argv[1], 128, 36, 10, 22, 0x1e1e2e)) return 1;
		long long b = loop_now();
		for (int i = 0; i < N; i++) if (stat(argv[1], &st)) return 1;
		long long c = loop_now();
		printf("hit %.0f ns, stat alone %.0f ns\n", (double)(b - a) / N, (double)(c - b) / N);
	}
	return 0;
}
