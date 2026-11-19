/* dumpj FILE...: json.c's faces of each file, in tests/test_json.lua's
   letters, into FILE.got (read line by line in 256-byte pieces, as
   tests/test_json.c's ours()) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "syntax.h"
#include "text.h"
static char code(unsigned char f)
{
	return f == FACE_BOOLEAN ? 'B' : f == FACE_CONSTANT_BUILTIN ? 'L' : f == FACE_NUMBER ? 'N'
	     : f == FACE_PROPERTY ? 'P' : f == FACE_STRING ? 'S' : f == FACE_STRING_ESCAPE ? 'E'
	     : f == FACE_COMMENT ? 'C' : f == FACE_PUNCTUATION_DELIMITER ? 'D'
	     : f == FACE_PUNCTUATION_BRACKET ? 'R' : f == FACE_NONE ? '.' : '?';
}
int main(int argc, char **argv)
{
	for (int k = 1; k < argc; k++) {
		FILE *in = fopen(argv[k], "rb");
		static char b[1 << 24];
		static unsigned char f[1 << 24];
		size_t n = fread(b, 1, sizeof b, in);
		fclose(in);
		memset(f, 0, n);
		Text *t = text_new(b, n);
		Syntax *s = syntax_open("x.json", t);
		syntax_update(s, t, NULL);
		for (size_t l = 0, nl = text_lines(t); l <= nl; l++) {
			size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
			for (size_t c = a; c < z; c += 256) syntax_faces(s, c, c + 256 < z ? c + 256 : z, f + c);
		}
		char path[512];
		snprintf(path, sizeof path, "%s.got", argv[k]);
		FILE *o = fopen(path, "wb");
		for (size_t i = 0; i < n; i++) fputc(code(f[i]), o);
		fclose(o);
		syntax_free(s);
		text_free(t);
	}
	return 0;
}
