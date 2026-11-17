/* Package 17's parsers on any bytes: git's status, diff, log and the diff and
   log buffers' output.  The model is the section tree's shape: every section
   lies inside its parent, its subtree ends where the next sibling starts,
   each line's section holds the line, and the shown text has one line per
   visible line, in order, after any sections are hidden. */
#include <stdint.h>
#include <string.h>
#include "check.h"
#include "../src/git/sec.h"

static void shape(View *v)
{
	for (int i = 0; i < v->nsec; i++) {
		Sec *s = &v->sec[i];
		CHECK(s->start <= s->end && s->end <= v->nln);
		CHECK(s->after > i && s->after <= v->nsec);
		if (s->parent >= 0) {
			Sec *p = &v->sec[s->parent];
			CHECK(s->parent < i && s->after <= p->after);
			CHECK(p->start <= s->start && s->end <= p->end);
		}
		for (int k = i + 1; k < s->after; k++) CHECK(v->sec[k].parent >= i);
	}
	for (size_t l = 0; l < v->nln; l++) {
		int i = v->ln[l].sec;
		CHECK(i >= 0 && i < v->nsec);
		CHECK(v->sec[i].start <= l && l < v->sec[i].end);
	}
	CHECK(!sec_render(v));
	size_t nl = 0;
	for (size_t k = 0; k < v->text.n; k++) nl += v->text.s[k] == '\n';
	CHECK(nl == v->nvis);
	for (size_t k = 1; k < v->nvis; k++) CHECK(v->vis[k - 1] < v->vis[k]);
	for (size_t k = 0; k < v->nvis; k++) CHECK(sec_vis_of(v, v->vis[k]) == k);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (!size) return 0;
	const char *s = (const char *)data + 1;
	size_t n = size - 1;
	Branch b = { 0 };
	Cache c = { 0 };
	View v = { .cache = &c };
	parse_status(s, n, &b);
	CHECK(sec_open(&v, T_STATUS, NULL, 0, 0) == 0);
	switch (data[0] % 6) {
	case 0: CHECK(parse_diff(&v, s, n, data[0] & 8, data[0] & 16, &b) >= 0); break;
	case 1: CHECK(!parse_log(&v, s, n, data[0] & 8 ? '<' : 0, &b)); break;
	case 2: CHECK(!parse_logbuf(&v, s, n, &b, 20 + data[0] % 100, 1800000000)); break;
	case 3: CHECK(!parse_diffbuf(&v, s, n, &b)); break;
	case 4: CHECK(!build_untracked(&v, &b)); break;
	case 5: {
		/* nested sections, hidden and shown at random, with the cache */
		for (size_t i = 0; i < n && v.nopen < 7; i++) {
			if (data[i + 1] & 1) CHECK(sec_open(&v, data[i + 1] % 16, s + i, 1, data[i + 1] & 2) >= 0);
			else CHECK(!sec_line(&v, s + i, 1));
			if (data[i + 1] & 4 && v.nopen > 1) sec_close(&v);
		}
		while (v.nopen > 1) sec_close(&v);
		break;
	}
	}
	while (v.nopen) sec_close(&v);
	shape(&v);
	for (int i = 0; i < v.nsec; i++) {
		v.sec[i].hidden = (data[i % size] >> (i % 8)) & 1;
		CHECK(!sec_cache_set(&v, i));
	}
	shape(&v);
	view_free(&v);
	branch_free(&b);
	str_free(&c.s);
	return 0;
}
