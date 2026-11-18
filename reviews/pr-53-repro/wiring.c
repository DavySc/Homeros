/* Review of PR 53: 'fileformat' against undo across :e, and against the
   syntax tree's byte offsets.
   wiring reload FILE A B   FILE holds A (R for CR, N for LF); an edit, :w, then the file is replaced by B outside, :e!,
     undo, :w.  Prints the format and lines at each step.
   wiring syntax LF.c CRLF.c   the two files hold the same lines; the faces
     of every byte of the two buffers must be equal. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core.h"
#include "syntax.h"
#include "undo.h"

/* R is CR and N is LF in the arguments, so the shell loses none */
static void put(const char *p, const char *s)
{
	FILE *f = fopen(p, "wb");
	for (; *s; s++) fputc(*s == 'R' ? '\r' : *s == 'N' ? '\n' : *s, f);
	fclose(f);
}
static void show(Editor *e, const char *tag)
{
	printf("%-28s ff=%s mod=%d lines", tag, e->buf->dos ? "dos" : "unix", core_modified(e->buf));
	for (size_t l = 0; l < core_lines(e); l++) {
		size_t n;
		const unsigned char *s = core_line(e, l, &n);
		printf(" [");
		for (size_t i = 0; i < n; i++) if (s[i] < 32) printf("^%c", s[i] + 64); else putchar(s[i]);
		printf("]");
	}
	printf("   msg=%s\n", strrchr(e->msg, '"') ? strrchr(e->msg, '"') + 1 : e->msg);
}

static size_t face_of(const char *path, unsigned char **out)
{
	static Editor e;
	if (core_open(&e, path) || !e.buf->syn) { fprintf(stderr, "no syntax for %s\n", path); exit(2); }
	while (syntax_update(e.buf->syn, e.buf->text, NULL) == 2) ;
	size_t n = text_len(e.buf->text);
	unsigned char *f = malloc(n + 1);
	for (size_t a = 0; a < n; a += 4096) syntax_faces(e.buf->syn, a, a + 4096 < n ? a + 4096 : n, f + a);
	printf("%s: %s, %zu bytes in the buffer\n", path, e.buf->dos ? "dos" : "unix", n);
	core_free(&e);
	*out = f;
	return n;
}

int main(int argc, char **argv)
{
	static Editor e;
	if (argc == 5 && !strcmp(argv[1], "reload")) {
		put(argv[2], argv[3]);
		if (core_open(&e, argv[2])) return 2;
		show(&e, "open");
		core_edit(&e, 0, 0, "x", 1);
		undo_close(e.buf->undo);   /* as a finished command does */
		core_write(&e, e.buf, 0);
		show(&e, "ix, :w");
		put(argv[2], argv[4]);
		core_edit_file(&e, NULL, 1);
		show(&e, "replaced outside, :e!");
		core_undo(&e, 0);
		show(&e, "u");
		core_write(&e, e.buf, 1);
		show(&e, ":w!");
		core_undo(&e, 1);
		show(&e, "CTRL-R");
		core_write(&e, e.buf, 1);
		show(&e, ":w!");
		core_free(&e);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "syntax")) {
		unsigned char *a, *b;
		size_t na = face_of(argv[2], &a), nb = face_of(argv[3], &b), coloured = 0, diff = 0;
		for (size_t i = 0; i < na && i < nb; i++) { coloured += a[i] != 0; diff += a[i] != b[i]; }
		printf("%zu and %zu bytes, %zu with a face, %zu faces differ\n", na, nb, coloured, diff);
		free(a); free(b);
		return na != nb || diff || !coloured;
	}
	return 2;
}
