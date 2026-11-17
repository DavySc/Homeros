/* Review of package 12: prints one face digit ('0' + face, as a char) per
   byte of lines [L0, L0+N) of FILE, newline kept.
   mode "full": the whole text is shown, parsed, and asked line by line.
   mode "win": as the editor does: the window's lines are asked, the parse
   runs, the window's lines are asked again (those are printed).
   usage: dump FILE full|win L0 N */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.h"
#include "text.h"

int main(int argc, char **argv)
{
	if (argc != 5) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) return 1;
	Text *t = text_new(b, (size_t)n);
	Syntax *s = syntax_open(argv[1], t);
	if (!s) return 3;
	size_t l0 = (size_t)atol(argv[3]), nl = (size_t)atol(argv[4]), lines = text_lines(t);
	if (l0 + nl > lines) nl = lines - l0;
	unsigned char *fc = malloc((size_t)n + 1);
	int full = !strcmp(argv[2], "full");
	for (int pass = 0; pass < 2; pass++) {
		if (full && !pass) syntax_faces(s, 0, (size_t)n, fc);
		else for (size_t l = l0; l < l0 + nl; l++) {
			size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
			syntax_faces(s, a, z, fc + a);
		}
		if (!pass) while (syntax_update(s, t, NULL)) ;
	}
	for (size_t l = l0; l < l0 + nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		for (size_t i = a; i < z; i++) putchar(b[i] == '\n' ? '\n' : '0' + fc[i]);
	}
	free(b);
	free(fc);
	syntax_free(s);
	text_free(t);
	return 0;
}
