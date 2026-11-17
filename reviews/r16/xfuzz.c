/* Reviewer's fuzzer for the package 16 comparison: both builds in one
   process on the same text.  Where they disagree one of them is not Org's
   parser; the text is kept in XDIR (one per kind of disagreement) for Emacs
   to judge (canon.el).  Input bytes pick org fragments (the union of both
   builds' fuzz tables and more), so short inputs make deep structure.
   With -DRAW the bytes are the text itself. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x.h"

static const char *const frag[] = { "* ", "** ", "*** ", "\n", "\n", "\n", " ", "  ", "\t", "- ", "+ ",
	"1. ", "2) ", " * ", "[ ] ", "[X] ", "[@3] ", " :: ", "#+begin_src", "#+end_src", "#+BEGIN_QUOTE",
	"#+END_QUOTE", "#+begin_foo", "#+end_foo", "#+BEGIN: x", "#+END:", "#+END", ":PROPERTIES:", ":END:",
	":LOGBOOK:", ":ID: x", "SCHEDULED: <2026-11-22 Sun .+1d>", "CLOCK: [2026-11-01 Sun 09:00]--[2026-11-01 Sun 10:30] =>  1:30",
	"# ", "#+NAME: x", "#+CAPTION[a]: b", "#+RESULTS:", "#+TBLFM: $1=2", "]", "(", ")", "| a |",
	"|-", "+--+", ": x", "-----", "%%(x)", "[fn:1] ", "\\begin{x}", "\\end{x}", "TODO ", "DONE ", "[#A] ",
	"COMMENT ", ":a:b:", "x", "y", "#", ":", "|", "[", "\r", "#+begin_center", "#+end_center", "#+begin_verse",
	"#+end_verse", "#+begin_example", "#+end_example", "#+begin_export", "#+end_export", "#+begin_comment", "#+end_comment",
	"#+", "#+ATTR_HTML: x", "#+HEADER: x", "#+TITLE: t", "#+x[y]:", "CLOSED: [2026-11-22 Sun]", "DEADLINE: <2026-11-22 Sun>",
	"CLOCK: ", "=> 1:00", "[2026-11-22 Sun]", "--", "[fn:a] ", "[fn:", "+-+", "+", "|", "%%", "\\begin{", "}", "\\end{",
	"NEXT ", "WAIT ", "CANCELLED ", "[#1] ", "[#12] ", "[#65]", "[#", "::", " ::", ":x:", ":x", "x:", "a.", "a) ", "10. ",
	"[@start:2] ", "[-] ", "[x] ", "    ", "        ", "\t\t", "\f", "*", "**", "******* ", "-", "----", "--------",
	": ", ":\n", "#\n", "# x\n", "\n\n", "\n\n\n", "#+begin_", "#+end_", "#+BEGIN:", "#+end:", "x y", ".", "$", "'", "%",
#ifdef CALLS
	"#+CALL: f[", "#+CALL: f(", "#+call: f",
#endif
#ifdef NONASCII
	"é", "É", " ", "→", "λ", "　", "ß",
#endif
};

static int differ(const XN *a, const XN *b, const char *s)
{
	if (a->type != b->type) return 1 + a->type * 64 + b->type * 2048;
	if (a->begin != b->begin || a->post != b->post || a->end != b->end || a->has != b->has || a->cbeg != b->cbeg || a->cend != b->cend) return 2 + a->type * 64;
	if (a->type == 0 && (a->level != b->level || a->todo != b->todo || a->pri != b->pri || a->commented != b->commented
	                     || a->tz - a->ta != b->tz - b->ta || memcmp(s + a->ta, s + b->ta, a->tz - a->ta) || a->taghash != b->taghash)) return 3;
	return 0;
}

static void check(const char *s, size_t n)
{
	XN *a, *b;
	size_t ka = xa_parse(s, n, &a), kb = xb_parse(s, n, &b);
	int d = 0;
	for (size_t i = 0; i < ka && i < kb && !d; i++) d = differ(&a[i], &b[i], s);
	if (!d && ka != kb) d = 4;
	if (d) {
		static unsigned char seen[1 << 18];
		const char *dir = getenv("XDIR");
		if (dir && seen[d] < 3) {   /* three texts of each kind */
			char name[512];
			snprintf(name, sizeof name, "%s/d%d-%d.org", dir, d, seen[d]++);
			FILE *f = fopen(name, "wb");
			if (f) { fwrite(s, 1, n, f); fclose(f); }
		}
	}
	free(a); free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
#ifdef RAW
	check((const char *)d, n);
#else
	static char s[1 << 14];
	size_t len = 0, nf = sizeof frag / sizeof *frag;
	for (size_t i = 0; i < n; i++) {
		const char *f = frag[d[i] % nf];
		size_t l = strlen(f);
		if (len + l > sizeof s) break;
		memcpy(s + len, f, l);
		len += l;
	}
	if (getenv("XSAVE")) { FILE *f = fopen(getenv("XSAVE"), "wb"); fwrite(s, 1, len, f); fclose(f); }
	check(s, len);
#endif
	return 0;
}
