/* PR 121 review: a parse stopped after the root's tree is in, then an edit
   that moves rows, then the parse finished.  The rows on_changedtree named
   before the edit (s->chg) are not moved with it.  Twin B gets the same
   edits and one unstopped parse.  Build as a test (see run.sh). */
#include <string.h>
#include "check.h"
#include "syntax.h"
#include "text.h"
static size_t r1, r2;
static int left;
static int stop(void) { return left-- <= 0; }
static void ed(Syntax *a, Syntax *b, Text *t, size_t pos, size_t del, const char *ins)
{
	if (a) syntax_edit(a, t, pos, del, ins, strlen(ins));
	if (b) syntax_edit(b, t, pos, del, ins, strlen(ins));
	CHECK(!text_delete(t, pos, del) && !text_insert(t, pos, ins, strlen(ins)));
}
int main(void)
{
	const char *src = "#define A(x) x + 1\n\n\n\n\nint f() ;\n\ta;\n\tb;\n}\n";
	int bad = 0;
	for (int k = 0; k < 60; k++) {
		Text *t = text_new(src, strlen(src));
		unsigned char f[256];
		Syntax *A = syntax_open("/tmp/x.c", t), *B = syntax_open("/tmp/x.c", t);
		CHECK(A && B);
		syntax_faces(A, 0, text_len(t), f), syntax_faces(B, 0, text_len(t), f);
		while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
		while (syntax_update(B, t, NULL, &r1, &r2) == 2) ;
		size_t semi = (size_t)(strchr(src, ';') - src);
		ed(A, B, t, semi, 1, "{");      /* a fold appears, no row added */
		ed(A, B, t, 13, 1, "y");        /* the #define body: its injected region is invalid */
		left = k;
		int r = syntax_update(A, t, stop, &r1, &r2);
		ed(A, B, t, 0, 0, "\n");        /* every row moves down one */
		while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
		while (syntax_update(B, t, NULL, &r1, &r2) == 2) ;
		int diff = 0;
		char la[128] = "", lb[128] = "";
		for (size_t l = 0; l < text_lines(t); l++) {
			int sa, sb, a = syntax_foldexpr(A, l, &sa), b = syntax_foldexpr(B, l, &sb);
			diff |= a != b || sa != sb;
			snprintf(la + strlen(la), 8, " %s%d", sa ? ">" : "", a);
			snprintf(lb + strlen(lb), 8, " %s%d", sb ? ">" : "", b);
		}
		if (diff) printf("k=%d (stopped update returned %d)\n  stopped:%s\n  whole:  %s\n", k, r, la, lb), bad++;
		syntax_free(A), syntax_free(B), text_free(t);
	}
	printf("%d of 60 stop points differ\n", bad);
	return bad != 0;
}
