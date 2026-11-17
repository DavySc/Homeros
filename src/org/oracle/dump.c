/* Prints src/org/'s parse of each file in oracle.el's format.  tests/test_org.c
   includes it (with NO_MAIN) to compare with lines recorded from Emacs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"

static FILE *OUT;

static const char *T[] = { "", "headline", "section", "planning", "property-drawer", "node-property",
	"drawer", "clock", "paragraph", "plain-list", "item", "table", "table-row", "keyword", "babel-call",
	"src-block", "example-block", "export-block", "comment-block", "quote-block", "center-block",
	"verse-block", "special-block", "dynamic-block", "comment", "fixed-width", "horizontal-rule",
	"latex-environment", "diary-sexp", "footnote-definition" };
static const char *buf;

static void num(int v, int last) { if (v < 0) fprintf(OUT, "-"); else fprintf(OUT, "%d", v); if (!last) fprintf(OUT, ","); }
static void unit(char u) { fprintf(OUT, "%c,", u ? u : '-'); }
static void ts(const OrgTimestamp *o, int ok)
{
	if (!ok) { fprintf(OUT, " -"); return; }
	fprintf(OUT, " %c,%c,%zu,%zu,", o->type, o->range ? o->range : '-', o->begin, o->end);
	num(o->y, 0); num(o->m, 0); num(o->d, 0); num(o->h, 0); num(o->min, 0);
	num(o->y2, 0); num(o->m2, 0); num(o->d2, 0); num(o->h2, 0); num(o->min2, 0);
	fprintf(OUT, "%c,", o->rep ? o->rep : '-');
	num(o->rep ? o->rep_n : -1, 0); unit(o->rep_unit);
	num(o->rep_dl_unit ? o->rep_dl_n : -1, 0); unit(o->rep_dl_unit);
	fprintf(OUT, "%c,", o->warn ? o->warn : '-');
	num(o->warn ? o->warn_n : -1, 0); fprintf(OUT, "%c", o->warn_unit ? o->warn_unit : '-');
}
static void str(size_t a, size_t b)
{
	fputc('"', OUT);
	for (; a < b; a++) {
		if (buf[a] == '"' || buf[a] == '\\') fputc('\\', OUT);
		if (buf[a] == '\n') fprintf(OUT, "\\n"); else fputc(buf[a], OUT);
	}
	fputc('"', OUT);
}
static int sp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static void pos(size_t p, int has) { if (has) fprintf(OUT, " %zu", p); else fprintf(OUT, " -"); }

static void print(FILE *out, const Text *t, const Org *o, const char *text)
{
	OUT = out; buf = text;
	const OrgNode *v = org_nodes(o);
	for (size_t k = 0; k < org_count(o); k++) {
		const OrgNode *e = &v[k];
		fprintf(OUT, "%s %zu %zu %zu", T[e->type], e->begin, e->end, e->post);
		pos(e->cbeg, e->cend != 0); pos(e->cend, e->cend != 0);
		if (e->type == ORG_HEADLINE) {
			OrgHeadline h;
			org_headline(t, e, &h);
			fprintf(OUT, " L%u T", e->level);
			if (h.todo_end > h.todo) fwrite(buf + h.todo, 1, h.todo_end - h.todo, OUT); else fprintf(OUT, "-");
			fprintf(OUT, " P"); num(h.priority, 1);
			fprintf(OUT, " C%d R", h.commented);
			size_t a = h.title, b = h.title_end;
			while (a < b && sp(buf[a])) a++;
			while (b > a && sp(buf[b - 1])) b--;
			str(a, b);
			fprintf(OUT, " G\"");
			/* org-split-string: a ':' first or last is not a separator */
			const char *g = buf + h.tags, *ge = buf + h.tags_end, *c2 = memchr(g, ':', (size_t)(ge - g));
			if (!c2) fwrite(g, 1, (size_t)(ge - g), OUT);
			else {
				int first = 1;
				if (c2 != g) { fwrite(g, 1, (size_t)(c2 - g), OUT); first = 0; }
				const char *i2 = c2 + 1;
				while ((c2 = memchr(i2, ':', (size_t)(ge - i2)))) {
					if (!first) fputc(':', OUT);
					fwrite(i2, 1, (size_t)(c2 - i2), OUT); first = 0;
					i2 = c2 + 1;
				}
				if (i2 != ge) { if (!first) fputc(':', OUT); fwrite(i2, 1, (size_t)(ge - i2), OUT); }
			}
			fputc('"', OUT);
		} else if (e->type == ORG_PLANNING) {
			OrgTimestamp p[3];
			int found = org_planning(t, e, p);
			for (int j = 0; j < 3; j++) ts(&p[j], found >> j & 1);
		} else if (e->type == ORG_CLOCK) {
			OrgTimestamp c; size_t d[2];
			ts(&c, org_clock(t, e, &c, d));
			if (d[1] > d[0]) { fputc(' ', OUT); str(d[0], d[1]); } else fprintf(OUT, " -");
		}
		fputc('\n', OUT);
	}
}

#ifndef NO_MAIN
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 1;
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		rewind(f);
		char *b = malloc((size_t)n + 1);
		if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 1;
		fclose(f);
		Text *t = text_new(b, (size_t)n);
		Org *o = org_new(t);
		if (!o) return 1;
		printf("== %s\n", argv[i]);
		print(stdout, t, o, b);
		org_free(o); text_free(t); free(b);
	}
	return 0;
}
#endif
