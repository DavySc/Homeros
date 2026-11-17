/* Review of PR 30, plant x1: a leaf whose external scanner state is over
   24 bytes keeps it on the heap (ts_external_scanner_state_delete).  No
   test or fuzz input has one: Markdown needs about 30 open blocks.  This
   parses 40 nested block quotes and frees; with x1 planted LeakSanitizer
   reports it, without it the run is clean.  usage: scanstate */
#include <string.h>
#include "syntax.h"
#include "text.h"
int main(void)
{
	char b[512];
	size_t n = 0;
	for (int l = 0; l < 3; l++) {
		for (int i = 0; i < 40; i++) b[n++] = '>', b[n++] = ' ';
		memcpy(b + n, "text\n", 5), n += 5;
	}
	Text *t = text_new(b, n);
	Syntax *s = syntax_open("f.md", t);
	unsigned char f[512];
	syntax_faces(s, 0, n, f);
	while (syntax_update(s, t, NULL)) ;
	syntax_edit(s, t, 0, n - 1, "", 0);        /* the old tree goes through release.c's steps */
	if (text_delete(t, 0, n - 1)) return 1;
	while (syntax_update(s, t, NULL)) ;
	syntax_free(s);
	text_free(t);
	return 0;
}
