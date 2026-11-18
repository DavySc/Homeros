/* Review of PR 51: our faces for one file, in tests/test_syntax.lua's output
   format (one char per byte, '0' + face, newlines kept), asked line by line
   from the first line as core_draw does.  Used by cmp.sh. */
#include <stdio.h>
#include <stdlib.h>
#include "syntax.h"
#include "text.h"
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	static char b[1 << 22];
	static unsigned char face[1 << 22];
	size_t n = fread(b, 1, sizeof b, f);
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[1], t);
	if (!t || !s) return 2;
	while (syntax_update(s, t, NULL)) ;
	for (size_t l = 0, nl = text_lines(t); l < nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		for (size_t x = a; x < z; x += 8192) syntax_faces(s, x, z - x < 8192 ? z : x + 8192, face + x);
	}
	for (size_t i = 0; i < n; i++) putchar(b[i] == '\n' ? '\n' : '0' + face[i]);
	syntax_free(s);
	text_free(t);
	return 0;
}
