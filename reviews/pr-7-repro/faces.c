/* core_draw with a grammar: each drawn cell must carry the face of its byte,
   also on lines longer than core_draw's 1024-byte face buffer and scrolled
   sideways; the terminal must send a colour and resend a cell whose face
   alone changed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "backend.h"
#include "core.h"
extern size_t fake_calls, fake_bytes;
#define CHECK(x) do { if (!(x)) { printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static size_t present(const Grid *g, Grid *shown, char *out, size_t max)
{
	int p[2], saved = dup(1);
	CHECK(!pipe(p)); fflush(stdout); dup2(p[1], 1);
	backend_tty.present(g, shown);
	dup2(saved, 1); close(p[1]); close(saved);
	ssize_t n = read(p[0], out, max - 1); close(p[0]);
	out[n > 0 ? n : 0] = 0;
	return n > 0 ? (size_t)n : 0;
}
int main(void)
{
	char path[] = "/tmp/facesXXXXXX"; int fd = mkstemp(path);
	size_t L = 5000;
	char *line = malloc(L + 1);
	for (size_t i = 0; i < L; i++) line[i] = (char)('a' + i % 26);
	line[L] = '\n';
	CHECK(write(fd, "short\n", 6) == 6 && write(fd, line, L + 1) == (ssize_t)(L + 1)); close(fd);
	Editor e; Grid g = { 0 }, shown = { 0 };
	CHECK(!core_open(&e, path) && e.buf->syn);
	CHECK(!grid_resize(&g, 24, 80));
	size_t cols[] = { 0, 1000, 1023, 1024, 1100, 2047, 2048, 4999 };
	for (size_t k = 0; k < sizeof cols / sizeof *cols; k++) {
		e.win->line = 1; e.win->col = cols[k];
		fake_calls = fake_bytes = 0;
		core_draw(&e, &g);
		int nw = 3, seen = 0;
		for (int c = nw + 1; c < g.cols; c++) {
			const Cell *x = &g.c[1 * g.cols + c];
			size_t byte = e.win->left + (size_t)(c - nw - 1);
			if (byte >= L) { CHECK(x->face == FACE_NONE); continue; }
			CHECK(x->ch[0] == line[byte]);
			CHECK(x->face == 1 + (6 + byte) % 10);
			seen++;
		}
		printf("cursor byte %4zu: left %4zu, %2d cells checked, syntax_faces called %zu times for %zu bytes (%d visible)\n",
		       cols[k], e.win->left, seen, fake_calls, fake_bytes, seen + 5);
	}
	/* the terminal */
	char out[1 << 16];
	e.win->line = 0; e.win->col = 0; core_draw(&e, &g);
	present(&g, &shown, out, sizeof out);
	CHECK(strstr(out, ";38;2;166;227;161m"));                 /* a FACE_STRING cell got green */
	CHECK(present(&g, &shown, out, sizeof out) < 40);         /* same frame: nothing sent */
	Cell *x = &g.c[4];                                        /* 's' of "short" */
	unsigned char old = x->face;
	x->face = FACE_KEYWORD == old ? FACE_TYPE : FACE_KEYWORD;
	present(&g, &shown, out, sizeof out);
	CHECK(strstr(out, x->face == FACE_KEYWORD ? ";38;2;203;166;247m" : ";38;2;249;226;175m") && strchr(out, 's'));
	puts("faces: cells carry their byte's face; the terminal colours and resends on a face change");
	core_free(&e); grid_free(&g); grid_free(&shown); unlink(path); free(line);
	return 0;
}
