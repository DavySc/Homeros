/* A .org file's colours are Org's (org.h's orgfx, package 16, which tests
   them against Emacs): through syntax_open, syntax_edit, syntax_update and
   syntax_faces, after random edits, every byte must carry the face an
   OrgFx of its own gives the same text. */
#include <string.h>
#include "check.h"
#include "grid.h"
#include "org.h"
#include "syntax.h"
#include "text.h"

enum { EDITS = 2000, MAXT = 1 << 14 };

static const char *const bits[] = {
	"* ", "** ", "TODO ", "DONE ", "[#A] ", "heading", ":tag:", "\n", "text ",
	"#+TITLE: x\n", "- item\n", "1. one\n", "#+begin_src c\n", "#+end_src\n",
	"SCHEDULED: <2026-11-20 Fri>\n", ":PROPERTIES:\n", ":END:\n", "# note\n", "|a|b|\n",
};

int main(void)
{
	static unsigned char f[MAXT], g[MAXT];
	const char *init = "* TODO [#A] heading :tag:\ntext\n** DONE b\n";
	Text *t = text_new(init, strlen(init)), *u = text_new(init, strlen(init));
	CHECK(t && u);
	Syntax *s = syntax_open("a/b.org", t);
	OrgFx *x = orgfx_new(u);
	CHECK(s && x);
	CHECK(syntax_update(s, t, NULL) == orgfx_update(x, NULL));
	syntax_faces(s, 0, 2, f);
	CHECK(f[0] != FACE_NONE);  /* the headline's star */
	for (int e = 0; e < EDITS; e++) {
		size_t n = text_len(t), pos = rnd((unsigned)n + 1), del = pos < n && rnd(3) == 0 ? rnd((unsigned)(n - pos) % 12 + 1) : 0;
		const char *ins = del ? "" : bits[rnd(sizeof bits / sizeof *bits)];
		size_t ni = strlen(ins);
		if (n - del + ni >= MAXT) continue;
		syntax_edit(s, t, pos, del, ins, ni);
		orgfx_edit(x, u, pos, del, ni);
		CHECK(!text_delete(t, pos, del) && !text_insert(t, pos, ins, ni));
		CHECK(!text_delete(u, pos, del) && !text_insert(u, pos, ins, ni));
		if (rnd(4)) continue;
		/* main.c redraws on 1 */
		CHECK(syntax_update(s, t, NULL) == orgfx_update(x, NULL));
		n = text_len(t);
		memset(g, 0, n);
		syntax_faces(s, 0, n, f);
		orgfx_faces(x, 0, n, g);
		CHECK(!memcmp(f, g, n));
	}
	syntax_free(s);
	orgfx_free(x);
	text_free(t);
	text_free(u);
	printf("test_syntax_org: %d edits, a .org file's faces equal Org's\n", EDITS);
	return 0;
}
