/* Shared by the reviewer's drivers for PR 45: compare org_edit's result with
   a whole parse and print a reproducible case when they differ. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static void esc(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '\n') fputs("\\n", stdout); else if (c == '\t') fputs("\\t", stdout);
		else if (c == '\\') fputs("\\\\", stdout); else if (c < 32 || c > 126) printf("\\x%02x", c); else putchar(c);
	}
}
/* index of the first differing node, or ORG_NONE */
static size_t differ(const OrgNode *e, size_t k, const OrgNode *f, size_t m)
{
	for (size_t i = 0; i < k && i < m; i++)
		if (e[i].begin != f[i].begin || e[i].aff != f[i].aff || e[i].cbeg != f[i].cbeg || e[i].cend != f[i].cend ||
		    e[i].end != f[i].end || e[i].parent != f[i].parent || e[i].type != f[i].type) return i;
	return k != m ? (k < m ? k : m) : ORG_NONE;
}
