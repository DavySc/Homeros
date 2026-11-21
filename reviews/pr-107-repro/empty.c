/* fold_update on a buffer with no lines (the core's CORE_EMPTY, text_lines()
   0) asks the foldexpr for line (size_t)-1. */
#include <stdio.h>
#include "fold.h"
static size_t n;
static int fe(void *c, size_t l, int *s)
{
	(void)c; (void)s;
	if (l >= n) printf("foldexpr asked for line %zu of %zu\n", l, n);
	return 0;
}
int main(void)
{
	Fold *f = fold_new(99, fe, NULL, NULL);
	fold_update(f, 0, 0, 0);          /* the first update, empty buffer */
	n = 3; fold_update(f, 0, 3, 3);
	n = 0; fold_adjust(f, 0, 3, FOLD_DEL, -3, 0);
	fold_update(f, 0, 0, 0);          /* after deleting every line */
	fold_free(f);
	return 0;
}
