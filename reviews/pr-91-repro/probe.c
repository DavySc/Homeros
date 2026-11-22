/* probe FILE FIRSTLINE NLINES [toplines]: faces of lines [FIRST, FIRST+N) as
   the editor would draw them with only that window visible (after first
   showing `toplines` lines from the top, when given), one char per byte as
   tests/test_syntax.lua renders them */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.h"
#include "text.h"
static void win(Syntax *s, Text *t, size_t l0, size_t nl, int print)
{
	static unsigned char f[1 << 16];
	static char b[1 << 16];
	for (int pass = 0; pass < 2; pass++) {
		for (size_t l = l0; l < l0 + nl && l < text_lines(t); l++) {
			size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
			syntax_faces(s, a, z, f);
			if (pass && print) {
				text_read(t, a, b, z - a);
				for (size_t i = 0; i < z - a; i++) putchar(b[i] == '\n' ? '\n' : '0' + f[i]);
			}
		}
		if (!pass) while (syntax_update(s, t, NULL)) ;
	}
}
int main(int argc, char **argv)
{
	FILE *fp = fopen(argv[1], "rb");
	static char buf[64 << 20];
	size_t n = fread(buf, 1, sizeof buf, fp);
	Text *t = text_new(buf, n);
	Syntax *s = syntax_open(argv[1], t);
	/* ops: w LINE N (show a window), e POS DEL STR (edit); the last w prints */
	for (int i = 2; i < argc; i += 4) {
		if (argv[i][0] == 'w') win(s, t, (size_t)atol(argv[i + 1]), (size_t)atol(argv[i + 2]), i + 4 >= argc), i--;
		else {
			size_t pos = (size_t)atol(argv[i + 1]), del = (size_t)atol(argv[i + 2]);
			syntax_edit(s, t, pos, del, argv[i + 3], strlen(argv[i + 3]));
			text_delete(t, pos, del); text_insert(t, pos, argv[i + 3], strlen(argv[i + 3]));
		}
	}
	syntax_free(s);
	return 0;
}
