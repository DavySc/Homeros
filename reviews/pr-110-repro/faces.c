/* Review of PR 110: FILE's first line and under it '.' where a byte has no
   face, '#' where it has one */
#include <stdio.h>
#include <stdlib.h>
#include "grid.h"
#include "syntax.h"
#include "text.h"

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[argc - 1], "rb");
	if (!f) return 1;
	static char b[8192];
	static unsigned char fa[8192];
	size_t n = fread(b, 1, sizeof b, f);
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[argc - 1], t);
	if (!s) return puts("none"), 1;
	size_t z = text_line_start(t, 1) - 1;
	syntax_faces(s, 0, z, fa);          /* asks for the window */
	syntax_update(s, t, NULL);
	syntax_faces(s, 0, z, fa);
	printf("%.*s\n", (int)z, b);
	for (size_t i = 0; i < z; i++) putchar(fa[i] == FACE_NONE ? '.' : '#');
	puts("");
	syntax_free(s);
	text_free(t);
	return 0;
}
