/* Review of PR 110: for each FILE, whether it gets colours, and each line's
   syntax_foldexpr as test_syntax.lua writes Neovim's (">n" where one starts) */
#include <stdio.h>
#include <stdlib.h>
#include "syntax.h"
#include "text.h"

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 1;
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		rewind(f);
		char *b = malloc((size_t)n + 1);
		if (fread(b, 1, (size_t)n, f) != (size_t)n) return 1;
		fclose(f);
		Text *t = text_new(b, (size_t)n);
		Syntax *s = syntax_open(argv[i], t);
		printf("%s %s", argv[i], s ? "colours" : "none");
		for (size_t l = 0; s && l < text_lines(t); l++) {
			int st, v = syntax_foldexpr(s, l, &st);
			printf(" %s%d", st ? ">" : "", v);
		}
		puts("");
		syntax_free(s);
		text_free(t);
		free(b);
	}
	return 0;
}
