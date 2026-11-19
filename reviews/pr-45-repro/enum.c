/* Every text of 1 to K lines over the line kinds below; for each, every
   line-level edit (replace a line by each kind, insert each kind before
   each line and at the end, delete one line, delete two) and every
   byte-level edit (delete each byte, insert each of "* \n#:-(" at each
   place).  org_edit from the old parse must equal org_parse of the new text.
   enum K [first-kind-lo first-kind-hi]   (the range splits the work) */
#include "common.h"
static const char *const kind[] = { "* a\n", "** b\n", "*** c\n", "x\n", "\n", "#+BEGIN_SRC c\n", "#+END_SRC\n",
	":PROPERTIES:\n", ":D:\n", ":END:\n", "- i\n", "  y\n", "#+CALL: f(\n", ")\n", "#+CALL: g[\n", "]\n", "| a |\n",
	"#+NAME: n\n", "SCHEDULED: <2026-01-01 Thu>\n", "[fn:1] z\n", "\\begin{e}\n", "\\end{e}\n", "# c\n", "* TODO d :t:", "z" };
enum { NK = sizeof kind / sizeof *kind };
static size_t edits, bad, wholes;
static void one(const char *s, size_t n, const OrgNode *e0, size_t k0, const char *t, size_t tn, size_t pos, size_t del, size_t ins)
{
	size_t k = k0, m;
	OrgNode *e = malloc((k0 ? k0 : 1) * sizeof *e), *f = org_parse(t, tn, &m);
	if (!e || !f) abort();
	memcpy(e, e0, k0 * sizeof *e);
	if (org_edit(&e, &k, t, tn, pos, del, ins)) abort();
	size_t d = differ(e, k, f, m);
	edits++;
	if (d != ORG_NONE && bad++ < 40) {
		printf("DIFF old \""); esc(s, n); printf("\" new \""); esc(t, tn);
		printf("\" pos %zu del %zu ins %zu: node %zu (%zu nodes, whole parse %zu)\n", pos, del, ins, d, k, m);
	}
	free(e); free(f);
}
static void all(const char *s, size_t n, const size_t *ls, int nl)
{
	static char t[4096];
	size_t k0;
	OrgNode *e0 = org_parse(s, n, &k0);
	if (!e0) abort();
	for (int i = 0; i <= nl; i++) {
		size_t a = ls[i], b = i < nl ? ls[i + 1] : n;
		for (int j = 0; j < NK; j++) {
			size_t l = strlen(kind[j]);
			memcpy(t, s, a); memcpy(t + a, kind[j], l); memcpy(t + a + l, s + a, n - a);          /* insert */
			one(s, n, e0, k0, t, n + l, a, 0, l);
			if (i < nl) { memcpy(t + a + l, s + b, n - b); one(s, n, e0, k0, t, n - (b - a) + l, a, b - a, l); }   /* replace */
		}
		if (i < nl) { memcpy(t, s, a); memcpy(t + a, s + b, n - b); one(s, n, e0, k0, t, n - (b - a), a, b - a, 0); }
		if (i + 1 < nl) { size_t c = ls[i + 2]; memcpy(t, s, a); memcpy(t + a, s + c, n - c); one(s, n, e0, k0, t, n - (c - a), a, c - a, 0); }
	}
	for (size_t p = 0; p <= n; p++) {
		if (p < n) { memcpy(t, s, p); memcpy(t + p, s + p + 1, n - p - 1); one(s, n, e0, k0, t, n - 1, p, 1, 0); }
		for (const char *c = "* \n#:-("; *c; c++) {
			memcpy(t, s, p); t[p] = *c; memcpy(t + p + 1, s + p, n - p);
			one(s, n, e0, k0, t, n + 1, p, 0, 1);
		}
	}
	free(e0);
}
int main(int argc, char **argv)
{
	int K = atoi(argv[1]), lo = argc > 3 ? atoi(argv[2]) : 0, hi = argc > 3 ? atoi(argv[3]) : NK;
	static char s[4096];
	size_t texts = 0;
	for (int nl = 1; nl <= K; nl++) {
		int ix[8] = { 0 };
		ix[0] = lo;
		for (;;) {
			size_t n = 0, ls[9];
			for (int i = 0; i < nl; i++) { ls[i] = n; size_t l = strlen(kind[ix[i]]); memcpy(s + n, kind[ix[i]], l); n += l; }
			ls[nl] = n;
			all(s, n, ls, nl); texts++;
			int i = nl - 1;
			while (i > 0 && ++ix[i] == NK) ix[i--] = 0;
			if (i == 0 && ++ix[0] == hi) break;
		}
	}
	(void)wholes;
	printf("%zu texts, %zu edits, %zu differ\n", texts, edits, bad);
	return bad != 0;
}
