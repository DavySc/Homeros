/* toks FILE: every token (tsql_token) with its kind, and the face of each byte */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "text.h"
#include "../../src/syntax/tsql.h"
static const char *kn[] = {"SPACE","WORD","IDENT","VAR","STRING","NUMBER","OP","PUNCT","COMMENT"};
static char code(unsigned char f)
{
	return f == FACE_KEYWORD ? 'K' : f == FACE_STRING ? 'S' : f == FACE_COMMENT ? 'C'
	     : f == FACE_FUNCTION_BUILTIN ? 'F' : f == FACE_MODULE_BUILTIN ? 'T' : f == FACE_FUNCTION ? 'P'
	     : f == FACE_OPERATOR ? 'o' : f == FACE_NONE ? '.' : '?';
}
int main(int argc, char **argv)
{
	static char b[1 << 20];
	static unsigned char f[1 << 20];
	FILE *in = argc > 1 ? fopen(argv[1], "rb") : stdin;
	size_t n = fread(b, 1, sizeof b, in);
	Text *t = text_new(b, n);
	Tsql *q = tsql_new(t);
	tsql_faces(q, 0, n, f);
	for (size_t i = 0; i < n; i++) putchar(b[i] == '\n' ? '\n' : b[i] == 0 ? '0' : b[i]);
	putchar('\n');
	for (size_t i = 0; i < n; i++) putchar(b[i] == '\n' ? '\n' : code(f[i]));
	putchar('\n');
	static TsqlRd rd;
	rd.t = t;
	for (size_t p = 0, e; p < n; p = e) {
		int k;
		e = tsql_token(&rd, p, &k);
		if (k != 0) printf("%s<%.*s> ", kn[k], (int)(e - p), b + p);
	}
	putchar('\n');
	return 0;
}
