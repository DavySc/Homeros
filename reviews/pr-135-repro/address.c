/* image.h says of image_wallpaper: "a new picture comes at a new address",
   and wl.c's wall_make keeps the address to know whether its buffer is
   current.  True for one caller that sees every change; a second window
   (the frames work) that misses one change can be handed the address it
   already holds for a different picture.  Window 2 last asked while
   picture A was current; the file then changes twice, window 1 asking after
   each; window 2 asks.  Prints whether the address is A's.
   Usage: address DIR N (an empty folder, uses DIR/homeros/wallpaper; pictures of N x N pixels) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "image.h"

/* a w x w grey PNG, stored (zlib level 0 is enough for stb) */
static void png(const char *f, int w, unsigned char v)
{
	char cmd[1024];
	snprintf(cmd, sizeof cmd, "python3 -c \"import zlib,struct;ch=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d));"
	         "open('%s','wb').write(b'\\x89PNG\\r\\n\\x1a\\n'+ch(b'IHDR',struct.pack('>IIBBBBB',%d,%d,8,0,0,0,0))+ch(b'IDAT',zlib.compress((b'\\0'+bytes([%d])*%d)*%d))+ch(b'IEND',b''))\"",
	         f, w, w, v, w, w);
	if (system(cmd)) exit(1);
}

int main(int argc, char **argv)
{
	char f[512];
	int w, h, src[4];
	if (argc < 3) return 1;
	int N = atoi(argv[2]);
	setenv("XDG_CONFIG_HOME", argv[1], 1);
	snprintf(f, sizeof f, "%s/homeros", argv[1]);
	mkdir(f, 0700);
	snprintf(f, sizeof f, "%s/homeros/wallpaper", argv[1]);
	png(f, N, 10);
	const uint32_t *a = image_wallpaper(100, 100, 0x1e1e2e, &w, &h, src);   /* both windows hold A */
	uint32_t a0 = a[0];
	png(f, N + 1, 100);
	const uint32_t *b = image_wallpaper(100, 100, 0x1e1e2e, &w, &h, src);   /* window 1: B */
	png(f, N, 200);
	const uint32_t *c = image_wallpaper(100, 100, 0x1e1e2e, &w, &h, src);   /* window 1: C */
	const uint32_t *d = image_wallpaper(100, 100, 0x1e1e2e, &w, &h, src);   /* window 2 */
	printf("A %p (pixel %06x)  B %p  C %p  window 2 gets %p (pixel %06x): %s\n", (void *)a, a0 & 0xffffff, (void *)b, (void *)c, (void *)d, d[0] & 0xffffff,
	       d == a ? "the address it holds, so it keeps picture A's buffer" : "a new address");
	return d == a;
}
