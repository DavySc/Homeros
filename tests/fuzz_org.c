/* libFuzzer: the org parser (src/org) against a simple model of the
   outline (every line of stars and a space is a headline, ending at the next
   one with as many stars or fewer), the tree's invariants, and the claim
   that a subtree parses alone to the same nodes.  Input bytes pick org
   fragments, so short inputs make deep structure; tools/org_conform.py
   checks the positions themselves against Org.  The colours (orgfx): read
   in windows of any size and order they are the whole text's, and after
   edits (an update after some) they are a fresh text's; before an update
   the bytes an edit did not touch keep their colours.  The fold levels
   (orgfx_foldexpr) after edits are a fresh text's.  A TODO step
   (org_todo_step) edits inside the text and leaves the heading at its
   level with the next keyword, its old one (blocked), or the one a repeat
   returns to (the title's first word when it is a keyword). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"

static void check_org(const char *s, size_t n)
{
	size_t k, m;
	OrgNode *e = org_parse(s, n, &k);
	if (!e) abort();
	size_t *last = malloc((k + 1) * sizeof *last), top = ORG_NONE, h = 0;   /* last child of each node */
	if (!last) abort();
	for (size_t j = 0; j < k; j++) {
		OrgNode *x = &e[j];
		last[j] = ORG_NONE;
		if (x->begin >= x->end || x->end > n || x->aff < x->begin || x->aff >= x->end) abort();
		if ((x->cbeg == ORG_NONE) != (x->cend == ORG_NONE)) abort();
		if (x->cbeg != ORG_NONE && (x->cbeg < x->aff || x->cbeg > x->cend || x->cend > x->end)) abort();
		if (j && x->begin < e[j - 1].begin) abort();
		size_t *prev = &top;
		if (x->parent != ORG_NONE) {
			OrgNode *p = &e[x->parent];
			if (x->parent >= j || p->cbeg == ORG_NONE || x->begin < p->cbeg || x->begin >= p->cend) abort();
			prev = &last[x->parent];
			if (*prev == ORG_NONE && x->begin != p->cbeg) abort();
		} else if (top == ORG_NONE) {
			size_t b = 0;
			while (b < n && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) b++;   /* org-skip-whitespace */
			while (b > 0 && s[b - 1] != '\n') b--;
			if (x->begin != b) abort();
		}
		if (*prev != ORG_NONE && x->begin != e[*prev].end) abort();
		*prev = j;
	}
	if (top != ORG_NONE && e[top].end != n) abort();
	/* the outline model */
	for (size_t l = 0; l < n; l = (memchr(s + l, '\n', n - l) ? (size_t)((const char *)memchr(s + l, '\n', n - l) - s) + 1 : n)) {
		size_t a = 0, z = n;
		while (l + a < n && s[l + a] == '*') a++;
		if (!a || l + a >= n || s[l + a] != ' ') continue;
		for (size_t q = l; (q = (memchr(s + q, '\n', n - q) ? (size_t)((const char *)memchr(s + q, '\n', n - q) - s) + 1 : n)) < n; ) {
			size_t b = 0;
			while (q + b < n && s[q + b] == '*') b++;
			if (b && b <= a && q + b < n && s[q + b] == ' ') { z = q; break; }
		}
		while (h < k && e[h].type != ORG_HEADLINE) h++;
		if (h == k || e[h].begin != l || e[h].end != z) abort();
		OrgHead d;
		org_head(s, n, l, &d);
		if (d.level != (int)a || d.ta > d.tz || d.tz > z || d.ga > d.gz || d.gz > z) abort();
		h++;
	}
	while (h < k && e[h].type != ORG_HEADLINE) h++;
	if (h != k) abort();
	/* a subtree alone: the same nodes, unless one reaches past it (a babel
	   call's bracket closing after the subtree) */
	for (size_t i = 0; i < k; i++) {
		if (e[i].type != ORG_HEADLINE) continue;
		size_t j = i, b = e[i].begin, far = 0;
		while (j < k && e[j].begin < e[i].end) far |= e[j++].end > e[i].end;
		if (far) continue;
		OrgNode *f = org_parse(s + b, e[i].end - b, &m);
		if (!f || m != j - i) abort();
		for (size_t x = 0; x < m; x++) {
			OrgNode *u = &e[i + x], *v = &f[x];
			if (u->type != v->type || u->begin - b != v->begin || u->aff - b != v->aff || u->end - b != v->end ||
			    (u->cbeg == ORG_NONE ? v->cbeg != ORG_NONE : u->cbeg - b != v->cbeg) ||
			    (u->cend == ORG_NONE ? v->cend != ORG_NONE : u->cend - b != v->cend) ||
			    (x ? u->parent - i != v->parent : v->parent != ORG_NONE)) abort();
		}
		free(f);
	}
	/* the timestamp readers stay inside the text and their node */
	for (size_t j = 0; j < k; j++) {
		OrgTs t[3];
		size_t d[2];
		int got = e[j].type == ORG_PLANNING ? org_planning(s, n, &e[j], t) :
		          e[j].type == ORG_CLOCK ? org_clock(s, n, &e[j], t, d) : 0;
		if (e[j].type == ORG_CLOCK && (d[0] > d[1] || d[1] > e[j].end)) abort();
		for (int i = 0; i < 3; i++)
			if (got >> i & 1 && (t[i].begin < e[j].begin || t[i].begin >= t[i].end || t[i].end > n || !strchr("aAiId", t[i].type))) abort();
	}
	for (size_t p = 0; p < n; p++) {
		OrgTs t;
		if (org_ts(s, n, p, &t) && (t.begin != p || t.end <= p || t.end > n || (t.type != 'd' && (t.m < 0 || t.d < 0)))) abort();
	}
	free(last);
	free(e);
}

/* org_edit: s with [pos, pos + del) replaced by ins bytes from s + q,
   reparsed in place, equals the new text parsed whole */
static void check_edit(const char *s, size_t n, size_t pos, size_t del, size_t q, size_t ins)
{
	static char t[1 << 15];
	size_t k, m, tn = pos + ins + (n - pos - del);
	memcpy(t, s, pos);
	memcpy(t + pos, s + q, ins);
	memcpy(t + pos + ins, s + pos + del, n - pos - del);
	OrgNode *e = org_parse(s, n, &k), *f = org_parse(t, tn, &m);
	if (!e || !f || org_edit(&e, &k, t, tn, pos, del, ins) || k != m) abort();
	for (size_t i = 0; i < k; i++)
		if (e[i].begin != f[i].begin || e[i].aff != f[i].aff || e[i].cbeg != f[i].cbeg || e[i].cend != f[i].cend ||
		    e[i].end != f[i].end || e[i].parent != f[i].parent || e[i].type != f[i].type) abort();
	free(e);
	free(f);
}

/* the colours of all n bytes, asked in windows of w from the end or the start */
static void painted(OrgFx *x, size_t n, size_t w, int back, unsigned char *f)
{
	memset(f, 0, n);
	for (size_t i = 0; i < n; i += w) {
		size_t a = back ? (n - i > w ? n - i - w : 0) : i, z = back ? n - i : (i + w < n ? i + w : n);
		orgfx_faces(x, a, z, f + a);
	}
}

static void check_fx(const char *s, size_t n, const uint8_t *d, size_t dn)
{
	static unsigned char a[1 << 15], b[1 << 15];
	static char t[1 << 15];
	Text *T = text_new(s, n), *U;
	OrgFx *x = orgfx_new(T), *y;
	if (!x || orgfx_update(x, NULL) != 1) abort();
	painted(x, n, n + 1, 0, a);
	painted(x, n, 1 + d[0] % 200, d[1] & 1, b);
	if (memcmp(a, b, n)) abort();
	int fresh = 1;   /* updated since the last edit */
	for (size_t i = 2; i + 4 <= dn && i < 14; i += 4) {   /* edits, from the input's first bytes */
		size_t pos = d[i] * (n + 1) / 256, del = d[i + 1] % 16, q = d[i + 2] * (n + 1) / 256, ins = d[i + 3] % 24;
		if (del > n - pos) del = n - pos;
		if (ins > n - q) ins = n - q;
		orgfx_edit(x, T, pos, del, ins);
		memcpy(t, s + q, ins);   /* s + q may be the text's own bytes: copied first */
		if (text_delete(T, pos, del) || text_insert(T, pos, t, ins)) abort();
		n = n - del + ins;
		painted(x, n, 1 + d[i] % 64, d[i + 1] & 1, b);
		for (size_t j = 0; fresh && j < n; j++)
			if (j < pos ? b[j] != a[j] : j >= pos + ins && b[j] != a[j - ins + del]) abort();
		if ((fresh = d[i + 2] & 1)) {
			if (orgfx_update(x, NULL) != 1) abort();
			painted(x, n, n + 1, 0, a);
		}
	}
	text_read(T, 0, t, n);
	U = text_new(t, n);
	y = orgfx_new(U);
	if (!y || orgfx_update(y, NULL) != 1) abort();
	for (size_t l = 0; l <= text_lines(T); l++) {   /* fold levels see the edits without an update */
		int p, q;
		if (orgfx_foldexpr(x, l, &p) != orgfx_foldexpr(y, l, &q) || p != q) abort();
	}
	painted(x, n, n + 1, 0, a);
	painted(y, n, n + 1, 0, b);
	if (memcmp(a, b, n)) abort();
	orgfx_free(x); orgfx_free(y); text_free(T); text_free(U);
}

static void check_todo(const char *s, size_t n, size_t pos, int dir)
{
	static char t[1 << 15];
	size_t a, b, len, k, h = ORG_NONE;
	const char *r = org_todo_step(s, n, pos, dir, 29918490, &a, &b, &len);
	if (!r || a > b || b > n || n - (b - a) + len > sizeof t) abort();
	memcpy(t, s, a);
	memcpy(t + a, r, len);
	memcpy(t + a + len, s + b, n - b);
	OrgNode *e = org_parse(s, n, &k);
	if (!e) abort();
	for (size_t i = 0; i < k; i++)   /* the innermost headline holding pos */
		if (e[i].type == ORG_HEADLINE && e[i].begin <= pos && (pos < e[i].end || e[i].end == n)) h = e[i].begin;
	free(e);
	if (h == ORG_NONE) { if (a != b || len) abort(); return; }
	OrgHead x, y;
	org_head(s, n, h, &x);
	org_head(t, n - (b - a) + len, h, &y);
	int to = dir > 0 ? (x.todo + 1 < 5 ? x.todo + 1 : -1) : x.todo == 0 ? -1 : x.todo < 0 ? 4 : x.todo - 1;
	const char *eol = memchr(s + h, '\n', n - h);
	if (x.commented || memchr(s + h, '\t', (eol ? (size_t)(eol - s) : n) - h)) return;   /* org-todo reads the keyword with
		COMMENT out ("COMMENT TODO a" holds TODO), and takes no tab around it where org-element does */
	if (to < 0 && y.todo >= 0 && !strncmp(s + x.ta, org_todo[y.todo], strlen(org_todo[y.todo]))) y.todo = -1;   /* "TODO DONE a" */
	if (y.level != x.level || (y.todo != to && y.todo != x.todo && !(to >= org_done && y.todo == (x.todo < 0 ? -1 : 0)))) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char *const frag[] = { "* ", "** ", "*** ", "\n", "\n", "\n", " ", "  ", "\t", "- ", "+ ",
		"1. ", "2) ", " * ", "[ ] ", "[X] ", "[@3] ", " :: ", "#+begin_src", "#+end_src", "#+BEGIN_QUOTE",
		"#+END_QUOTE", "#+begin_foo", "#+end_foo", "#+BEGIN: x", "#+END:", "#+END", ":PROPERTIES:", ":END:",
		":LOGBOOK:", ":ID: x", "SCHEDULED: <2026-11-22 Sun .+1d>", "CLOCK: [2026-11-01 Sun 09:00]--[2026-11-01 Sun 10:30] =>  1:30",
		"# ", "#+NAME: x", "#+CAPTION[a]: b", "#+RESULTS:", "#+TBLFM: $1=2", "#+CALL: f[", "]", "(", ")", "<2026-11-22 Sun 9:00-10:30 .+1d/3d -2d>", "--", "<%%(x) 9:00>", "CLOSED: ", "DEADLINE: ", " => 1:30", "[2026-01-02]", "| a |",
		"|-", "+--+", ": x", "-----", "%%(x)", "[fn:1] ", "\\begin{x}", "\\end{x}", "TODO ", "DONE ", "[#A] ",
		"COMMENT ", ":a:b:", "x", "y", "é", "#", ":", "|", "[", "\r" };
	static char s[1 << 14];
	size_t len = 0, nf = sizeof frag / sizeof *frag;
	for (size_t i = 0; i < n; i++) {
		const char *f = d[i] < nf ? frag[d[i]] : (const char[]){ (char)("ab x*-+:|#[]\n\t"[d[i] % 14]), 0 };
		size_t l = strlen(f);
		if (len + l > sizeof s) break;
		memcpy(s + len, f, l);
		len += l;
	}
	check_org(s, len);
	if (n >= 4) {
		size_t pos = d[0] * (len + 1) / 256, del = d[1] % 16, q = d[2] * (len + 1) / 256, ins = d[3] % 24;
		if (del > len - pos) del = len - pos;
		if (ins > len - q) ins = len - q;
		check_edit(s, len, pos, del, q, ins);
		check_fx(s, len, d, n);
		check_todo(s, len, pos, d[1] & 1 ? 1 : -1);
	}
	return 0;
}
