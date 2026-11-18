/* Build A's parse of each file in canon.el's format (reviewer's driver). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static const char *T[] = { "", "headline", "section", "planning", "property-drawer", "node-property",
	"drawer", "clock", "paragraph", "plain-list", "item", "table", "table-row", "keyword", "babel-call",
	"src-block", "example-block", "export-block", "comment-block", "quote-block", "center-block",
	"verse-block", "special-block", "dynamic-block", "comment", "fixed-width", "horizontal-rule",
	"latex-environment", "diary-sexp", "footnote-definition" };
static void pos(size_t p, int has) { if (has) printf(" %zu", p); else printf(" -"); }
static void hex(const char *b, size_t a, size_t z) { for (; a < z; a++) printf("%02x", (unsigned char)b[a]); }
static int sp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 2;
		fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
		char *b = malloc((size_t)n + 1);
		if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
		fclose(f);
		Text *t = text_new(b, (size_t)n);
		Org *o = org_new(t);
		if (!o) return 3;
		const char *base = strrchr(argv[i], '/');
		printf("== %s\n", base ? base + 1 : argv[i]);
		const OrgNode *v = org_nodes(o);
		for (size_t k = 0; k < org_count(o); k++) {
			const OrgNode *e = &v[k];
			printf("%s %zu %zu", T[e->type], e->begin, e->post);
			pos(e->cbeg, e->cend != 0); pos(e->cend, e->cend != 0);
			printf(" %zu\n", e->end);
			if (e->type != ORG_HEADLINE) continue;
			OrgHeadline h;
			org_headline(t, e, &h);
			printf("+ %u ", e->level);
			if (h.todo_end > h.todo) fwrite(b + h.todo, 1, h.todo_end - h.todo, stdout); else printf("-");
			if (h.priority < 0) printf(" -"); else printf(" %d", h.priority);
			printf(" %s ", h.commented ? "c" : "-");
			size_t a = h.title, z = h.title_end;
			while (a < z && sp(b[a])) a++;
			while (z > a && sp(b[z - 1])) z--;
			hex(b, a, z);
			printf(". ");
			/* org-split-string, as the header says to: a ':' first or last is not a separator */
			const char *g = b + h.tags, *ge = b + h.tags_end, *c2 = memchr(g, ':', (size_t)(ge - g));
			if (!c2) hex(g, 0, (size_t)(ge - g));
			else {
				int first = 1;
				if (c2 != g) { hex(g, 0, (size_t)(c2 - g)); first = 0; }
				const char *i2 = c2 + 1;
				while ((c2 = memchr(i2, ':', (size_t)(ge - i2)))) {
					if (!first) printf("3a");
					hex(i2, 0, (size_t)(c2 - i2)); first = 0;
					i2 = c2 + 1;
				}
				if (i2 != ge) { if (!first) printf("3a"); hex(i2, 0, (size_t)(ge - i2)); }
			}
			printf(".\n");
		}
		org_free(o); text_free(t); free(b);
	}
	return 0;
}
