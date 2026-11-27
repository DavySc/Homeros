/* Review of PR 136: a text whose first token is "?" (zig.zig reads tokenTag(k - 1) with k = 0) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct ZigH ZigH;
#define REL(v) ZigH *zig##v##_parse(const char *, size_t, unsigned char *); void zig##v##_free(ZigH *);
REL(15) REL(16) REL(17)
int main(int c, char **v)
{
	const char *t = c > 1 ? v[1] : "?u8";
	size_t n = strlen(t);
	char *src = malloc(n + 1); unsigned char *f = malloc(n + 1);
	memcpy(src, t, n + 1);
	ZigH *h;
	h = zig15_parse(src, n, f); fputs("15 ok\n", stderr); zig15_free(h);
	h = zig16_parse(src, n, f); fputs("16 ok\n", stderr); zig16_free(h);
	h = zig17_parse(src, n, f); fputs("17 ok\n", stderr); zig17_free(h);
	return 0;
}
