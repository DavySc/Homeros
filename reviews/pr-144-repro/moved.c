/* review, second pass: a deletion that takes a kept start with it, in a text whose kept starts alternate between
   code and a long comment; the faces after it against a fresh index.  With tsql_edit's ", q->st[i + k - j] = q->st[k]"
   removed (the PR's "# Not planted" line) the kept starts keep their neighbours' states. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "tsql.h"
#include "mk.h"
int main(void)
{
	static char b[200000];
	static unsigned char f[200000], g[200000];
	size_t n = 0, bad = 0, tries = 0;
	for (int k = 0; k < 12; k++) {
		size_t m = n + 3000 + (size_t)k * 500;
		while (n < m) n += (size_t)sprintf(b + n, "def a := 1\n");
		n += (size_t)sprintf(b + n, "/-\n");
		m = n + 6000 - (size_t)k * 300;
		while (n < m) n += (size_t)sprintf(b + n, "some words\n");
		n += (size_t)sprintf(b + n, "-/\n");
	}
	for (size_t at = 0; at + 6000 < n; at += 1777, tries++) {
		Text *t = text_new(b, n);
		Tsql *q = mk(t, 3);
		while (tsql_update(q, NULL) == 2) ;
		size_t p = text_line_start(t, text_line_of(t, at)), d = text_line_start(t, text_line_of(t, at + 4200)) - p;
		tsql_edit(q, t, p, d, 0);
		if (text_delete(t, p, d)) return 2;
		while (tsql_update(q, NULL) == 2) ;
		size_t z = text_len(t), L = text_lines(t);
		memset(f, 0, z), memset(g, 0, z);
		/* a screen at a time from the end, as jumps would */
		for (size_t l = L; l > 0; l = l > 50 ? l - 50 : 0)
			for (size_t i = l > 50 ? l - 50 : 0; i < l; i++) {
				size_t a = text_line_start(t, i), e = text_line_start(t, i + 1);
				tsql_faces(q, a, e, f + a);
			}
		Tsql *h = mk(t, 3);
		while (tsql_update(h, NULL) == 2) ;
		tsql_faces(h, 0, z, g);
		if (memcmp(f, g, z)) bad++;
		tsql_free(h), tsql_free(q), text_free(t);
	}
	printf("moved: %zu of %zu deletions leave faces that differ from a fresh read\n", bad, tries);
	return bad != 0;
}
