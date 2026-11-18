/* Build B's parse of each file in canon.el's format (reviewer's driver). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static const char *const names[] = { "headline", "section", "planning", "property-drawer",
	"node-property", "drawer", "clock", "comment", "keyword", "babel-call", "paragraph",
	"plain-list", "item", "table", "table-row", "src-block", "example-block", "export-block",
	"comment-block", "verse-block", "quote-block", "center-block", "special-block",
	"dynamic-block", "latex-environment", "fixed-width", "horizontal-rule",
	"footnote-definition", "diary-sexp" };
static void pos(size_t p) { if (p == ORG_NONE) printf(" -"); else printf(" %zu", p); }
static void hex(const char *b, size_t a, size_t z) { for (; a < z; a++) printf("%02x", (unsigned char)b[a]); }
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 2;
		fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
		char *b = malloc((size_t)n + 1);
		if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
		fclose(f);
		size_t k;
		OrgNode *e = org_parse(b, (size_t)n, &k);
		if (!e) return 3;
		const char *base = strrchr(argv[i], '/');
		printf("== %s\n", base ? base + 1 : argv[i]);
		for (size_t j = 0; j < k; j++) {
			printf("%s", names[e[j].type]);
			pos(e[j].begin); pos(e[j].aff); pos(e[j].cbeg); pos(e[j].cend); pos(e[j].end);
			printf("\n");
			if (e[j].type != ORG_HEADLINE) continue;
			OrgHead h;
			org_head(b, (size_t)n, e[j].begin, &h);
			printf("+ %d %s ", h.level, h.todo < 0 ? "-" : org_todo[h.todo]);
			if (h.pri < 0) printf("-"); else printf("%d", h.pri);
			printf(" %s ", h.commented ? "c" : "-");
			hex(b, h.ta, h.tz); printf(". "); hex(b, h.ga, h.gz); printf(".\n");
		}
		free(e); free(b);
	}
	return 0;
}
