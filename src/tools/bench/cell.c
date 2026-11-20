/* For thread 0's question on faces[256] (2026-11-20): what a uint16_t face
   would cost the frame diff.  Compares two unchanged 50 x 200 frames cell by
   cell with grid_cell_eq's test (the backends' common case: every cell is
   compared), Cell as now (8 bytes) and with face widened (FACE16).
   Prints sizeof(Cell) and ns per frame, the median of 2001 frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef FACE16
typedef struct { unsigned char ch[4], len, attr, bg; unsigned short face; } Cell;
#else
typedef struct { unsigned char ch[4], len, attr, face, bg; } Cell;
#endif
enum { N = 50 * 200, F = 2001 };

__attribute__((noinline)) static int eq(const Cell *a, const Cell *b)
{
	return a->len == b->len && a->attr == b->attr && a->face == b->face && a->bg == b->bg && !memcmp(a->ch, b->ch, a->len < 4 ? a->len : 4);
}

static int cmp(const void *a, const void *b) { return (*(const long *)a > *(const long *)b) - (*(const long *)a < *(const long *)b); }

int main(void)
{
	static Cell a[N], b[N];
	static long t[F];
	for (int i = 0; i < N; i++) a[i] = (Cell){ .ch = { (unsigned char)('a' + i % 26) }, .len = 1, .face = (unsigned char)(i % 180) };
	memcpy(b, a, sizeof a);
	volatile int same = 0;
	for (int f = 0; f < F; f++) {
		struct timespec s, e;
		clock_gettime(CLOCK_MONOTONIC, &s);
		int n = 0;
		for (int i = 0; i < N; i++) n += eq(&a[i], &b[i]);
		clock_gettime(CLOCK_MONOTONIC, &e);
		same += n;
		t[f] = (e.tv_sec - s.tv_sec) * 1000000000L + e.tv_nsec - s.tv_nsec;
	}
	qsort(t, F, sizeof *t, cmp);
	printf("%zu %ld\n", sizeof(Cell), t[F / 2]);
	return 0;
}
