/* Build A behind x.h.  A's enum is xnames' order plus one. */
#include <stdlib.h>
#include <string.h>
#include "org.h"
#include "x.h"
static int sp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
size_t xa_parse(const char *s, size_t n, XN **out)
{
	Text *t = text_new(s, n);
	Org *o = t ? org_new(t) : NULL;
	if (!o) abort();
	size_t k = org_count(o);
	XN *x = calloc(k + 1, sizeof *x);
	const OrgNode *v = org_nodes(o);
	for (size_t i = 0; i < k; i++) {
		x[i] = (XN){ v[i].begin, v[i].post, v[i].cbeg, v[i].cend, v[i].end, v[i].cend != 0, v[i].type - 1, 0, -1, -1, 0, 0, 0, 0 };
		if (!x[i].has) x[i].cbeg = x[i].cend = 0;
		if (v[i].type != ORG_HEADLINE) continue;
		OrgHeadline h;
		org_headline(t, &v[i], &h);
		static const char *const kw[] = { "TODO", "NEXT", "WAIT", "DONE", "CANCELLED" };
		x[i].level = (int)v[i].level;
		for (int j = 0; j < 5; j++)
			if (h.todo_end - h.todo == strlen(kw[j]) && !memcmp(s + h.todo, kw[j], h.todo_end - h.todo)) x[i].todo = j;
		x[i].pri = h.priority; x[i].commented = h.commented;
		size_t a = h.title, z = h.title_end;
		while (a < z && sp(s[a])) a++;
		while (z > a && sp(s[z - 1])) z--;
		x[i].ta = a; x[i].tz = z;
		/* tags as org-split-string leaves them: drop one ':' at each end if there */
		size_t g = h.tags, ge = h.tags_end;
		if (g < ge && s[g] == ':') g++;
		if (ge > g && s[ge - 1] == ':') ge--;
		unsigned hh = 5381;
		for (; g < ge; g++) hh = hh * 33 + (unsigned char)s[g];
		x[i].taghash = h.tags_end > h.tags ? hh : 0;
	}
	org_free(o); text_free(t);
	*out = x;
	return k;
}
