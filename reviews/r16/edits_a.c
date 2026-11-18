/* Reviewer's test of build A's incremental reparse on real-sized texts:
   random edits to each file given (bytes, lines, snippets inserted; ranges
   of 0 to 3,000 bytes deleted, across headlines too), org_edit after each,
   and the list compared with a fresh org_new of the same text.
   edits_a SEED N FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static const char *P[] = { "\n", "* ", "** ", "*** h\n", "*", " ", "\t", ":END:\n", ":PROPERTIES:\n", ":ID: x\n", "#+BEGIN_SRC c\n",
	"#+END_SRC\n", "#+begin_quote\n", "#+end_quote\n", "- ", "1. ", "| a |\n", "|-", "SCHEDULED: <2026-11-22 Sun .+1d/3d>\n",
	"CLOCK: [2026-11-01 Sun 10:00]--[2026-11-01 Sun 11:00] =>  1:00\n", "#+NAME: t\n", "[fn:1] ", "# c\n", ": f\n",
	"-----\n", "\\begin{x}\n", "\\end{x}\n", "TODO ", ":a:", "x", "\n\n", "é", "#+TBLFM: x\n", "\n* TODO new :t:\nbody\n", "\n\n\n" };
int main(int argc, char **argv)
{
	srand((unsigned)atoi(argv[1]));
	int n = atoi(argv[2]);
	size_t edits = 0;
	for (int i = 3; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		fseek(f, 0, SEEK_END); size_t k = (size_t)ftell(f); rewind(f);
		char *b = malloc(k + 1);
		if (fread(b, 1, k, f) != k) return 2;
		fclose(f);
		Text *t = text_new(b, k);
		Org *o = org_new(t);
		for (int j = 0; j < n; j++) {
			size_t len = text_len(t), pos = (size_t)rand() % (len + 1), del = 0;
			int kind = rand() % 10;
			const char *s = "";
			if (kind < 5) s = P[(size_t)rand() % (sizeof P / sizeof *P)];
			else if (kind < 8) del = (size_t)rand() % 8;
			else if (kind < 9) del = (size_t)rand() % 3000;
			else { s = P[(size_t)rand() % (sizeof P / sizeof *P)]; del = (size_t)rand() % 200; }
			if (del > len - pos) del = len - pos;
			size_t ins = strlen(s);
			if (text_delete(t, pos, del) || text_insert(t, pos, s, ins)) return 3;
			if (org_edit(o, t, pos, del, ins)) return 3;
			Org *full = org_new(t);
			size_t c = org_count(o);
			const OrgNode *x = org_nodes(o), *y = org_nodes(full);
			if (c != org_count(full)) { printf("%s edit %d at %zu del %zu ins %zu: %zu nodes, fresh %zu\n", argv[i], j, pos, del, ins, c, org_count(full)); return 1; }
			for (size_t q = 0; q < c; q++)
				if (x[q].begin != y[q].begin || x[q].end != y[q].end || x[q].post != y[q].post || x[q].cbeg != y[q].cbeg
				    || x[q].cend != y[q].cend || x[q].type != y[q].type || x[q].depth != y[q].depth || x[q].level != y[q].level) {
					printf("%s edit %d at %zu del %zu ins %zu: node %zu differs\n", argv[i], j, pos, del, ins, q); return 1; }
			org_free(full);
			edits++;
		}
		org_free(o); text_free(t); free(b);
	}
	printf("%zu edits, every list equal to a fresh parse\n", edits);
	return 0;
}
