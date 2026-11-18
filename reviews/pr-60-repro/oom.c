/* fails the N-th allocation inside sqlfmt, for every N: it must return -1
   (sqlfmt.h) and the next call must work.  Built with
   -Wl,--wrap=realloc,--wrap=malloc,--wrap=calloc under ASan.  A fresh process per N (the buffers stay grown).
   usage: oom FILE N */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "sqlfmt.h"
void *__real_realloc(void *, size_t); void *__real_malloc(size_t); void *__real_calloc(size_t, size_t);
static long count, fail_at = -1;
static int hit(void) { return ++count == fail_at; }
void *__wrap_realloc(void *p, size_t n) { return hit() ? NULL : __real_realloc(p, n); }
void *__wrap_malloc(size_t n) { return hit() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return hit() ? NULL : __real_calloc(a, b); }
int main(int argc, char **argv)
{
	static char buf[1 << 22], want[1 << 23];
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(buf, 1, sizeof buf, f), wn = 0;
	(void)argc;
	int wrong = 0, total = 0;
	for (long at = atol(argv[2]); ; at++, total++) {
		Text *t = text_new(buf, n);
		size_t a = 0, z = n, m; const char *r;
		count = 0, fail_at = at;
		int k = sqlfmt(t, &a, &z, &r, &m);
		int failed = count >= at;
		fail_at = -1;
		if (failed && k != -1) wrong++, printf("allocation %ld failed: sqlfmt returned %d, edit [%zu,%zu) -> %zu bytes\n", at, k, a, z, m);
		/* and the call after it gives the right answer */
		a = 0, z = n;
		k = sqlfmt(t, &a, &z, &r, &m);
		size_t on = n - (z - a) + m;
		static char o[1 << 23];
		memcpy(o, buf, a); memcpy(o + a, r, m); memcpy(o + a + m, buf + z, n - z);
		fwrite(o, 1, on, stderr);
		text_free(t);
		break;
	}
	printf("%ld allocations in the call\n", count);
	return wrong != 0;
}
