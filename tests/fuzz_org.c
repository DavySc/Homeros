/* libFuzzer: the org parser's incremental reparse (org_edit) against the
   simple model, a full parse of the edited text (org_new); and the shape of
   the list on any input: in order, nested, inside the text. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"

static const char *P[] = { "\n", "* ", "** ", "*", " ", "\t", ":END:", ":PROPERTIES:", "\n:ID: x\n", "#+BEGIN_SRC c\n",
	"#+END_SRC", "#+begin_quote\n", "#+end_quote\n", "- ", "1. ", "| a |\n", "|-", "SCHEDULED: <2026-11-22 Sun .+1d/3d>",
	"CLOCK: [2026-11-01 Sun 10:00]--[2026-11-01 Sun 11:00] =>  1:00", "#+NAME: t\n", "[fn:1] ", "# c\n", ": f\n",
	"-----\n", "\\begin{x}\n", "\\end{x}\n", "%%(d)\n", "TODO ", ":a:", "x", "\n\n", "é", "#+TBLFM: x\n", "+--+\n" };

static void same(const Org *a, const Org *b)
{
	size_t n = org_count(a);
	if (n != org_count(b)) abort();
	const OrgNode *x = org_nodes(a), *y = org_nodes(b);
	for (size_t i = 0; i < n; i++)
		if (x[i].begin != y[i].begin || x[i].end != y[i].end || x[i].post != y[i].post || x[i].cbeg != y[i].cbeg
		    || x[i].cend != y[i].cend || x[i].type != y[i].type || x[i].depth != y[i].depth || x[i].level != y[i].level) abort();
}

static void shape(const Org *o, size_t len)
{
	const OrgNode *v = org_nodes(o);
	size_t n = org_count(o), up[512];
	for (size_t i = 0; i < n; i++) {
		const OrgNode *e = &v[i];
		if (e->begin > e->post || e->post > e->end || e->end > len) abort();
		if (e->cend && (e->cbeg < e->post || e->cbeg > e->cend || e->cend > e->end)) abort();
		if (i && v[i - 1].begin > e->begin) abort();
		if (e->depth >= 512 || (i ? e->depth > v[i - 1].depth + 1 : e->depth)) abort();
		if (e->depth) {
			const OrgNode *p = &v[up[e->depth - 1]];
			/* starts inside its parent; a table's #+TBLFM lines may run past it, as in Org */
			if (!p->cend || e->begin < p->cbeg || e->begin >= p->cend) abort();
		}
		up[e->depth] = i;
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char buf[1 << 16];
	size_t k = n < 64 ? n : 64, len = k;
	memcpy(buf, d, k);
	Text *t = text_new(buf, len);
	Org *o = org_new(t);
	if (!t || !o) abort();
	shape(o, len);
	for (size_t i = k; i + 3 <= n; i += 3) {
		size_t pos = (size_t)d[i + 1] * (len + 1) / 256, del = d[i] & 1 ? 0 : d[i + 2] % 8;
		const char *s = d[i] & 1 ? P[d[i + 2] % (sizeof P / sizeof *P)] : "";
		size_t ins = strlen(s);
		if (del > len - pos) del = len - pos;
		if (len - del + ins > sizeof buf) break;
		if (text_delete(t, pos, del) || text_insert(t, pos, s, ins)) abort();
		len = len - del + ins;
		if (org_edit(o, t, pos, del, ins)) abort();
		text_read(t, 0, buf, len);
		Org *full = org_new(t);
		if (!full) abort();
		shape(o, len);
		same(o, full);
		org_free(full);
	}
	org_free(o);
	text_free(t);
	return 0;
}
