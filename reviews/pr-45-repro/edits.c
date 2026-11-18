/* Random edits chained on real-sized texts: after each, org_edit's list
   must equal org_parse's of the same text.  Half the positions are taken
   at or next to a node's begin, contents or end (so heading, block and
   drawer boundaries are hit far more often than uniformly), the rest
   anywhere.  The text goes back to the file every RESET edits.
   edits SEED N RESET FILE... */
#include "common.h"
static const char *P[] = { "\n", "* ", "** ", "*** h\n", "*", " ", "\t", ":END:\n", ":PROPERTIES:\n", ":ID: x\n", ":LOGBOOK:\n",
	"#+BEGIN_SRC c\n", "#+END_SRC\n", "#+begin_quote\n", "#+end_quote\n", "#+BEGIN_EXAMPLE\n", "#+END_EXAMPLE\n", "- ", "1. ", "  ",
	"| a |\n", "|-", "SCHEDULED: <2026-11-22 Sun .+1d/3d>\n", "CLOCK: [2026-11-01 Sun 10:00]--[2026-11-01 Sun 11:00] =>  1:00\n",
	"#+NAME: t\n", "[fn:1] ", "# c\n", ": f\n", "-----\n", "\\begin{x}\n", "\\end{x}\n", "TODO ", ":a:", "x", "\n\n", "\xc3\xa9",
	"#+TBLFM: x\n", "\n* TODO new :t:\nbody\n", "\n\n\n", "#+CALL: f(x\n", "#+CALL: g[a\n", "#+call: h()\n", "(", ")", "[", "]",
	"#+BEGIN: d\n", "#+END:\n", "* top\n", "\n** sub\n", "#+", "\r" };
static unsigned long long rs;
static size_t rnd(size_t n) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (size_t)((rs >> 33) % (n ? n : 1)); }
int main(int argc, char **argv)
{
	rs = strtoull(argv[1], NULL, 10);
	size_t n = (size_t)atol(argv[2]), reset = (size_t)atol(argv[3]), edits = 0;
	for (int i = 4; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		if (!fp) { perror(argv[i]); return 2; }
		fseek(fp, 0, SEEK_END); size_t len0 = (size_t)ftell(fp); rewind(fp);
		size_t cap = len0 + n * 64 + 1;
		char *orig = malloc(len0 + 1), *t = malloc(cap), *old = malloc(cap);
		if (fread(orig, 1, len0, fp) != len0) return 2;
		fclose(fp);
		size_t len = 0, k = 0, m;
		OrgNode *e = NULL;
		for (size_t j = 0; j < n; j++) {
			if (j % reset == 0) { free(e); memcpy(t, orig, len0); len = len0; e = org_parse(t, len, &k); if (!e) return 3; }
			size_t pos, del = 0, kind = rnd(10);
			if (k && rnd(2)) {
				const OrgNode *x = &e[rnd(k)];
				size_t c[5] = { x->begin, x->aff, x->cbeg, x->cend, x->end }, p = c[rnd(5)];
				if (p == ORG_NONE) p = x->end;
				pos = p + rnd(3);
				pos = pos ? pos - 1 : 0;
				if (pos > len) pos = len;
			} else pos = rnd(len + 1);
			const char *s = "";
			if (kind < 5) s = P[rnd(sizeof P / sizeof *P)];
			else if (kind < 8) del = rnd(8);
			else if (kind < 9) del = rnd(3000);
			else { s = P[rnd(sizeof P / sizeof *P)]; del = rnd(200); }
			if (del > len - pos) del = len - pos;
			size_t ins = strlen(s), oldlen = len;
			memcpy(old, t, len);
			memmove(t + pos + ins, t + pos + del, len - pos - del);
			memcpy(t + pos, s, ins);
			len = len - del + ins;
			if (org_edit(&e, &k, t, len, pos, del, ins)) return 3;
			OrgNode *f = org_parse(t, len, &m);
			if (!f) return 3;
			size_t d = differ(e, k, f, m);
			if (d != ORG_NONE) {
				printf("%s edit %zu at %zu del %zu ins %zu \"", argv[i], j, pos, del, ins); esc(s, ins);
				printf("\": node %zu differs (%zu nodes, whole parse %zu)\n", d, k, m);
				FILE *o = fopen("fail-old.org", "wb"); fwrite(old, 1, oldlen, o); fclose(o);
				o = fopen("fail-new.org", "wb"); fwrite(t, 1, len, o); fclose(o);
				return 1;
			}
			free(f);
			edits++;
		}
		free(e); free(orig); free(t); free(old);
	}
	printf("%zu edits, every list equal to a whole parse\n", edits);
	return 0;
}
