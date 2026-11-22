/* libFuzzer: the PR's fold.c against the shorter variant (reviews/pr-107-repro/shorter.diff),
   the same edits, Insert-mode lines, fold commands and splits on both; every
   line's fold_closed() and fold_level() and every fold_cmd() result must agree. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fold.h"
#define D(r, name, args) r name args; r s_##name args;
D(Fold *, fold_new, (int, FoldExpr *, FoldTall *, void *))
D(Fold *, fold_dup, (const Fold *, void *))
D(void, fold_free, (Fold *))
D(void, fold_adjust, (Fold *, size_t, size_t, long, long, int))
D(void, fold_update, (Fold *, size_t, size_t, size_t))
D(int, fold_closed, (const Fold *, size_t, size_t *, size_t *))
D(int, fold_level, (const Fold *, size_t))
D(int, fold_cmd, (Fold *, int, size_t, long))
enum { MAXN = 64 };
static unsigned char lv[MAXN];
static size_t n;
static int expr(void *c, size_t l, int *s) { (void)c; if (l >= n) abort(); *s = lv[l] >= 32; return lv[l] % 32; }
static int tall(void *c, size_t l) { (void)c; return l % 3 == 0; }
static void same(Fold *f, Fold *g)
{
	size_t a1, a2, b1, b2;
	for (size_t l = 0; l < n; l++) {
		int c = fold_closed(f, l, &a1, &a2);
		if (c != s_fold_closed(g, l, &b1, &b2) || (c && (a1 != b1 || a2 != b2))) abort();
		if (fold_level(f, l) != s_fold_level(g, l)) abort();
	}
}
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t len)
{
	size_t i = 0;
#define NEXT (i < len ? d[i++] : 0)
	n = 1 + NEXT % 20;
	for (size_t l = 0; l < n; l++) lv[l] = NEXT % 56;
	int level = NEXT % 4, t = NEXT & 1;
	Fold *f = fold_new(level, expr, t ? tall : NULL, NULL), *g = s_fold_new(level, expr, t ? tall : NULL, NULL);
	fold_update(f, 0, n, n); s_fold_update(g, 0, n, n);
	same(f, g);
	while (i < len) {
		unsigned op = NEXT % 4;
		if (op == 0) {
			size_t a = NEXT % (n + 1), b = a + NEXT % 4, k = NEXT % 5;
			if (b > n) b = n;
			if (n - (b - a) + k < 1) k = 1;
			if (n - (b - a) + k > MAXN) k = 0;
			if (n - (b - a) + k < 1) continue;
			memmove(lv + a + k, lv + b, n - b);
			for (size_t j = 0; j < k; j++) lv[a + j] = NEXT % 56;
			long extra = (long)k - (long)(b - a);
			n = (size_t)((long)n + extra);
			fold_adjust(f, a, b, b > a ? FOLD_DEL : 0, extra, 0); s_fold_adjust(g, a, b, b > a ? FOLD_DEL : 0, extra, 0);
			fold_update(f, a, (size_t)((long)b + extra), n); s_fold_update(g, a, (size_t)((long)b + extra), n);
		} else if (op == 1 && n < MAXN) {
			size_t at = NEXT % (n + 1);
			memmove(lv + at + 1, lv + at, n - at);
			lv[at] = NEXT % 56;
			n++;
			fold_adjust(f, at, (size_t)-1, 1, 0, 1); s_fold_adjust(g, at, (size_t)-1, 1, 0, 1);
		} else if (op == 2) {
			int c = NEXT % (FOLD_LEVEL + 1);
			if (c == FOLD_RESET) { fold_update(f, 0, n, n); s_fold_update(g, 0, n, n); }
			size_t at = NEXT % n;
			long cnt = NEXT % 4;
			if (fold_cmd(f, c, at, cnt) != s_fold_cmd(g, c, at, cnt)) abort();
		} else {
			Fold *f2 = fold_dup(f, NULL), *g2 = s_fold_dup(g, NULL);
			fold_free(f); s_fold_free(g);
			f = f2; g = g2;
		}
		same(f, g);
	}
	fold_free(f); s_fold_free(g);
	return 0;
}
