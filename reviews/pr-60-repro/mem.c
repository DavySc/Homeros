#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include "text.h"
#include "sqlfmt.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	static char buf[1 << 26];
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(buf, 1, sizeof buf, f);
	Text *t = text_new(buf, n);
	struct mallinfo2 m0 = mallinfo2();
	size_t a = 0, z = n, m; const char *r;
	sqlfmt(t, &a, &z, &r, &m);
	struct mallinfo2 m1 = mallinfo2();
	printf("held after one format of %zu bytes: %zu KB (heap %zu + mmap %zu)\n", n, (m1.uordblks + m1.hblkhd - m0.uordblks - m0.hblkhd) / 1024, (m1.uordblks - m0.uordblks) / 1024, (m1.hblkhd - m0.hblkhd) / 1024);
	for (int i = 0; i < 5; i++) { double t0 = now(); Text *o = text_new(buf, n); double t1 = now(); text_free(o); printf("text_new+free of the same size: %.2f ms\n", t1 - t0); }
	(void)argc; return 0;
}
