/* The cluster store against a plain model: random clusters put in a grid, every cell's
   grid_text and grid_cell_eq compared with the bytes the model keeps, through many frames
   (grid_frame empties the store; the grid is then redrawn from the model, as the contract asks). */
#include <string.h>
#include "check.h"
#include "grid.h"
enum { R = 40, C = 120, POOL = 200000 };
static unsigned char model[R * C][GRID_CLUSTER_MAX]; static unsigned char mlen[R * C];
static unsigned char pool[POOL][44]; static unsigned char plen[POOL];
static size_t cut(const unsigned char *s, size_t n)
{
	if (n <= GRID_CLUSTER_MAX) return n;
	for (n = GRID_CLUSTER_MAX; n > 1 && (s[n] & 0xC0) == 0x80; n--) {}
	return n;
}
int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 300, distinct = argc > 2 ? atoi(argv[2]) : 3000;
	long resets = 0, puts_ = 0, fffd = 0;
	for (int i = 0; i < POOL; i++) {          /* a base letter and combining marks of 2 or 3 bytes */
		size_t n = 0;
		pool[i][n++] = (unsigned char)('a' + rnd(26));
		for (int k = (int)rnd(14); k > 0 && n + 3 < sizeof pool[i]; k--)
			if (rnd(2)) { pool[i][n++] = 0xCC; pool[i][n++] = (unsigned char)(0x80 + rnd(48)); }
			else { pool[i][n++] = 0xE2; pool[i][n++] = 0x83; pool[i][n++] = (unsigned char)(0x90 + rnd(32)); }
		plen[i] = (unsigned char)n;
	}
	Grid g = { 0 };
	CHECK(!grid_resize(&g, R, C));
	for (int i = 0; i < R * C; i++) { model[i][0] = ' '; mlen[i] = 1; }
	for (int f = 0; f < frames; f++) {
		if (grid_frame()) {                    /* numbers are stale: draw everything again */
			resets++;
			for (int i = 0; i < R * C; i++) CHECK(grid_put(&g, i / C, i % C, model[i], mlen[i], 1, 0) == 1);
		}
		for (int k = 0; k < 4000; k++) {
			unsigned i = rnd(R * C), p = rnd((unsigned)distinct);
			CHECK(grid_put(&g, (int)(i / C), (int)(i % C), pool[p], plen[p], 1, 0) == 1);
			puts_++;
			size_t n = cut(pool[p], plen[p]);
			const Cell *x = &g.c[i];
			if (x->len == 3 && n != 3 && !memcmp(grid_text(x), "\xef\xbf\xbd", 3)) { fffd++; memcpy(model[i], "\xef\xbf\xbd", 3); mlen[i] = 3; continue; }
			memcpy(model[i], pool[p], n); mlen[i] = (unsigned char)n;
		}
		for (int i = 0; i < R * C; i++) {
			CHECK(g.c[i].len == mlen[i] && !memcmp(grid_text(&g.c[i]), model[i], mlen[i]));
			unsigned j = rnd(R * C);
			int same = mlen[i] == mlen[j] && !memcmp(model[i], model[j], mlen[i]);
			CHECK(grid_cell_eq(&g.c[i], &g.c[j]) == same);
		}
	}
	printf("%d frames, %ld puts from %d distinct clusters: every cell's text and equality match the model; store emptied %ld times, U+FFFD given %ld times\n", frames, puts_, distinct, resets, fffd);
	grid_free(&g);
	return 0;
}
