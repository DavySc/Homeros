#include <stdio.h>
#include <string.h>
#include "../../src/syntax/tsql.h"
#include "../../src/syntax/tsql_words.h"
int main(void)
{
	size_t nw = sizeof words / sizeof *words, nc = sizeof classes / sizeof *classes, re = 0, bad = 0, kw = 0, fn = 0, op = 0, mx = 0;
	for (size_t i = 0; i < nw; i++) {
		if (i && strcmp(words[i - 1], words[i]) >= 0) bad++, printf("order: %s %s\n", words[i - 1], words[i]);
		for (const char *p = words[i]; *p; p++) if (*p >= 'A' && *p <= 'Z') bad++, printf("upper: %s\n", words[i]);
		if (strlen(words[i]) > mx) mx = strlen(words[i]);
		if (i < nc) { re += !!(classes[i] & TQ_RECASE); kw += classes[i] & 1; fn += !!(classes[i] & 2); op += !!(classes[i] & 4); if (!classes[i] || classes[i] > 15) bad++; }
		if (i < nc && (classes[i] & TQ_RECASE) && !(classes[i] & (TQ_KEYWORD|TQ_OPERATOR))) printf("recase not keyword: %s\n", words[i]);
	}
	printf("%zu words, %zu classes, sizeof classes elt %zu, %zu recase, %zu kw, %zu fn, %zu op, longest %zu, %zu bad\n", nw, nc, sizeof *classes, re, kw, fn, op, mx, bad);
	printf("operator words:"); for (size_t i = 0; i < nw; i++) if (classes[i] & 4) printf(" %s", words[i]); puts("");
	printf("recase:"); for (size_t i = 0; i < nw; i++) if (classes[i] & 8) printf(" %s", words[i]); puts("");
	printf("kw+fn:"); for (size_t i = 0; i < nw; i++) if ((classes[i] & 3) == 3) printf(" %s", words[i]); puts("");
	return 0;
}
