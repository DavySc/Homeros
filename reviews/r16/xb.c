/* Build B behind x.h. */
#include <stdlib.h>
#include "org.h"
#include "x.h"
static const int map[] = { 0, 1, 2, 3, 4, 5, 6, 23, 12, 13, 7, 8, 9, 10, 11, 14, 15, 16, 17, 20, 18, 19, 21, 22, 26, 24, 25, 28, 27 };
size_t xb_parse(const char *s, size_t n, XN **out)
{
	size_t k;
	OrgNode *e = org_parse(s, n, &k);
	if (!e) abort();
	XN *x = calloc(k + 1, sizeof *x);
	for (size_t i = 0; i < k; i++) {
		int has = e[i].cbeg != ORG_NONE;
		x[i] = (XN){ e[i].begin, e[i].aff, has ? e[i].cbeg : 0, has ? e[i].cend : 0, e[i].end, has, map[e[i].type], 0, -1, -1, 0, 0, 0, 0 };
		if (e[i].type != ORG_HEADLINE) continue;
		OrgHead h;
		org_head(s, n, e[i].begin, &h);
		x[i].level = h.level; x[i].todo = h.todo; x[i].pri = h.pri; x[i].commented = h.commented;
		x[i].ta = h.ta; x[i].tz = h.tz;
		unsigned hh = 5381;
		for (size_t g = h.ga; g < h.gz; g++) hh = hh * 33 + (unsigned char)s[g];
		x[i].taghash = h.gz > h.ga ? hh : 0;
	}
	free(e);
	*out = x;
	return k;
}
