/* a frame larger than the backend's 64 KiB output buffer, and allocation in the redraw path */
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include "check.h"
#include "backend.h"
#include "test_tty_vt.h"
static unsigned char out[1 << 22];
static int w1(const unsigned char *s, int n) { (void)s; return n == 3 ? 2 : 1; }
static size_t mallocs;
void *__real_malloc(size_t); void *__wrap_malloc(size_t n) { mallocs++; return __real_malloc(n); }
void *__real_calloc(size_t, size_t); void *__wrap_calloc(size_t a, size_t b) { mallocs++; return __real_calloc(a, b); }
void *__real_realloc(void *, size_t); void *__wrap_realloc(void *p, size_t n) { mallocs++; return __real_realloc(p, n); }
static size_t present(const Grid *g, Grid *shown)
{
	int f = open("/tmp/big.out", O_RDWR | O_CREAT | O_TRUNC, 0600), saved = dup(1);
	dup2(f, 1);
	backend_present(g, shown);
	dup2(saved, 1); close(saved);
	ssize_t n = pread(f, out, sizeof out, 0);
	close(f);
	return (size_t)n;
}
int main(void)
{
	static Vt t;
	Grid g = { 0 }, shown = { 0 };
	CHECK(!grid_resize(&g, 64, 256));
	vt_init(&t, 64, 256, w1);
	size_t biggest = 0, after = 0;
	for (int f = 0; f < 200; f++) {
		for (int r = 0; r < 64; r++)
			for (int c = 0; c < 256;) {
				unsigned k = rnd(3);
				const char *s = k == 0 ? "\xc3\xa9" : k == 1 ? "\xe6\xbc\xa2" : "\xf0\x9d\x93\x9d";
				int w = grid_put(&g, r, c, (const unsigned char *)s, strlen(s), k == 1 ? 2 : 1, (int)rnd(3));
				c += w ? w : 1;
			}
		if (f == 1) after = mallocs;
		size_t n = present(&g, &shown);
		if (n > biggest) biggest = n;
		vt_feed(&t, out, n);
		const char *why = vt_differs(&t, &g);
		if (why) { fprintf(stderr, "frame %d: %s\n", f, why); return 1; }
	}
	grid_free(&g); grid_free(&shown);
	fprintf(stderr, "200 frames of 64x256 non-ASCII cells shown exactly; largest frame %zu bytes; malloc/calloc/realloc calls in frames 2..200: %zu\n", biggest, mallocs - after);
	return 0;
}
