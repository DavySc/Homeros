/* libFuzzer: the fold module on any mix of edits (as Neovim's
   nvim_buf_set_lines() and Insert mode's new lines call it), fold commands
   and splits.  Every fold found lies around its line and inside the
   buffer, a split window's copy answers as the original, and folds built
   fresh have each line's level from the foldexpr (clamped at 20, Neovim's
   MAX_LEVEL); tests/test_fold.c holds the rest against Neovim.  Edits leave
   Neovim's tree out of shape at times (a nested fold past its parent), so
   that is not checked. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fold.h"

enum { MAXN = 64 };
static unsigned char lv[MAXN];   /* a line: its level, +32 for ">n" */
static size_t n;

static int expr(void *ctx, size_t l, int *start)
{
	(void)ctx;
	if (l >= n) abort();
	*start = lv[l] >= 32;
	return lv[l] % 32;
}

static int tall(void *ctx, size_t l)
{
	(void)ctx;
	return l % 3 == 0;
}

static void check(Fold *f)
{
	size_t l1, l2;
	for (size_t l = 0; l < n; l++)
		if (fold_closed(f, l, &l1, &l2) && (l1 > l || l2 < l || l2 >= n)) abort();
}

static void same(Fold *f, Fold *g)
{
	size_t a1, a2, b1, b2;
	for (size_t l = 0; l < n; l++) {
		int c = fold_closed(f, l, &a1, &a2);
		if (c != fold_closed(g, l, &b1, &b2) || (c && (a1 != b1 || a2 != b2))) abort();
		if (fold_level(f, l) != fold_level(g, l)) abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t len)
{
	size_t i = 0;
#define NEXT (i < len ? d[i++] : 0)
	n = NEXT % 21;
	for (size_t l = 0; l < n; l++) lv[l] = NEXT % 56;
	int level = NEXT % 4;
	Fold *f = fold_new(level, expr, NEXT & 1 ? tall : NULL, NULL);
	if (!f) abort();
	fold_update(f, 0, n, n);
	for (size_t l = 0; l < n; l++)
		if (fold_level(f, l) != (lv[l] % 32 < 20 ? lv[l] % 32 : 20)) abort();
	while (i < len) {
		unsigned op = NEXT % 4;
		if (op == 0) {   /* nvim_buf_set_lines(a, b): k new lines */
			size_t a = NEXT % (n + 1), b = a + NEXT % 4, k = NEXT % 5;
			if (b > n) b = n;
			if (n - (b - a) + k > MAXN) k = 0;
			memmove(lv + a + k, lv + b, n - b);
			for (size_t j = 0; j < k; j++) lv[a + j] = NEXT % 56;
			long extra = (long)k - (long)(b - a);
			n = (size_t)((long)n + extra);
			fold_adjust(f, a, b, b > a ? FOLD_DEL : 0, extra, 0);
			fold_update(f, a, (size_t)((long)b + extra), n);
		} else if (op == 1 && n < MAXN) {   /* a line opened in Insert mode */
			size_t at = NEXT % (n + 1);
			memmove(lv + at + 1, lv + at, n - at);
			lv[at] = NEXT % 56;
			n++;
			fold_adjust(f, at, (size_t)-1, 1, 0, 1);
		} else if (op == 2) {
			int c = NEXT % (FOLD_LEVEL + 1);
			if (c == FOLD_RESET) fold_update(f, 0, n, n);
			size_t at = NEXT % (n ? n : 1);
			fold_cmd(f, c, at, NEXT % 4);
		} else {         /* a split: the copy goes on in f's place */
			Fold *g = fold_dup(f, NULL);
			if (!g) abort();
			same(f, g);
			fold_free(f);
			f = g;
		}
		check(f);
	}
	fold_free(f);
	return 0;
}
