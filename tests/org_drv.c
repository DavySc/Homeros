/* Prints src/org's parse of each file as tools/org_dump.el prints Org's
   (tools/org_conform.py compares them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/org/org.h"

static const char *const names[] = { "headline", "section", "planning", "property-drawer",
	"node-property", "drawer", "clock", "comment", "keyword", "babel-call", "paragraph",
	"plain-list", "item", "table", "table-row", "src-block", "example-block", "export-block",
	"comment-block", "verse-block", "quote-block", "center-block", "special-block",
	"dynamic-block", "latex-environment", "fixed-width", "horizontal-rule",
	"footnote-definition", "diary-sexp" };

static void pos(FILE *o, size_t p) { if (p == ORG_NONE) fprintf(o, " -"); else fprintf(o, " %zu", p); }

/* the nodes as tools/org_dump.el prints them; 0, or -1 out of memory */
static int dump(FILE *o, const char *b, size_t n)
{
	size_t k;
	OrgNode *e = org_parse(b, n, &k);
	if (!e) return -1;
	for (size_t j = 0; j < k; j++) {
		fprintf(o, "%s", names[e[j].type]);
		pos(o, e[j].begin); pos(o, e[j].aff); pos(o, e[j].cbeg); pos(o, e[j].cend); pos(o, e[j].end);
		fprintf(o, "\n");
		if (e[j].type != ORG_HEADLINE) continue;
		OrgHead h;
		org_head(b, n, e[j].begin, &h);
		fprintf(o, "+ %d %s ", h.level, h.todo < 0 ? "-" : org_todo[h.todo]);
		if (h.pri < 0) fprintf(o, "-"); else fprintf(o, "%d", h.pri);
		fprintf(o, " %s \"", h.commented ? "c" : "-");
		for (size_t x = h.ta; x < h.tz; x++) {
			if (b[x] == '"' || b[x] == '\\') fputc('\\', o);
			fputc(b[x], o);
		}
		fprintf(o, "\" ");
		fprintf(o, "%.*s", (int)(h.gz - h.ga), b + h.ga);   /* the tags joined by ":" */
		fprintf(o, "\n");
	}
	free(e);
	return 0;
}

#ifndef ORG_DRV_LIB
int main(int argc, char **argv)
{
	static char b[1 << 24];
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 2;
		size_t n = fread(b, 1, sizeof b, f);
		int more = fgetc(f) != EOF;
		fclose(f);
		if (more) { fprintf(stderr, "%s: over %zu bytes\n", argv[i], sizeof b); return 4; }
		const char *base = strrchr(argv[i], '/');
		printf("== %s\n", base ? base + 1 : argv[i]);
		if (dump(stdout, b, n)) return 3;
	}
	return 0;
}
#endif
