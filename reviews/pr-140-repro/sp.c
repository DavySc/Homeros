#include <stdio.h>
#include <stdlib.h>
#include "find.h"
static void show(unsigned c, int w, int h, int t) {
	unsigned char *o = malloc(w*h); sprite_draw(c, w, h, t, o);
	printf("U+%04X %dx%d t=%d\n", c, w, h, t);
	for (int y = 0; y < h; y++) { for (int x = 0; x < w; x++) putchar(o[y*w+x] == 255 ? '#' : o[y*w+x] ? '+' : '.'); putchar('\n'); }
	free(o);
}
int main(int argc, char **argv) { int w = atoi(argv[1]), h = atoi(argv[2]), t = atoi(argv[3]); for (int i = 4; i < argc; i++) show(strtoul(argv[i], 0, 16), w, h, t); }
