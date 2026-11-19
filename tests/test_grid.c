/* The terminal backend sends only changed cells: present a frame, present it
   again (no cell output), change one cell (exactly that cell is sent).  Its
   input keeps a character split across reads and never drops a byte when
   there are fewer key slots than bytes waiting. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "backend.h"

static char out[1 << 16];

static size_t present(const Grid *g, Grid *shown)
{
	int p[2], saved = dup(1);
	CHECK(!pipe(p));
	dup2(p[1], 1);
	backend_tty.present(g, shown);
	dup2(saved, 1);
	close(p[1]);
	close(saved);
	ssize_t n = read(p[0], out, sizeof out - 1);
	close(p[0]);
	CHECK(n > 0);
	out[n] = 0;
	return (size_t)n;
}

/* feed s through stdin and collect the keys, max slots per call */
static int input(const char *s, size_t n, int *k, int max)
{
	int p[2], saved = dup(0), got = 0, r;
	CHECK(!pipe(p) && write(p[1], s, n) == (ssize_t)n);
	close(p[1]);
	dup2(p[0], 0);
	close(p[0]);
	while ((r = backend_tty.input(k + got, max)) > 0) got += r;
	dup2(saved, 0);
	close(saved);
	return got;
}

int main(void)
{
	static char in[1024];
	static int k[1024];
	in[0] = 'a';
	for (int i = 0; i < 200; i++) memcpy(in + 1 + 2 * i, "\xc3\xa9", 2);   /* 401 bytes: a read splits one */
	CHECK(input(in, 401, k, 256) == 201 && k[0] == 'a');
	for (int i = 1; i <= 200; i++) CHECK(k[i] == 0xE9);
	memset(in, 'a', 300);
	CHECK(input(in, 300, k, 100) == 300);

	Grid g = { 0 }, shown = { 0 };
	CHECK(!grid_resize(&g, 4, 10));
	grid_puts(&g, 0, 0, g.cols, "hello", 5, ATTR_NONE, FACE_NONE);
	present(&g, &shown);
	CHECK(strstr(out, "hello") && strstr(out, "\x1b[?2026h") && strstr(out, "\x1b[?2026l"));
	size_t idle = present(&g, &shown);
	CHECK(!strstr(out, "hello") && idle < 40);          /* only the wrapper and cursor */
	grid_put(&g, 2, 3, (const unsigned char *)"\xc3\xa9", 2, 1, ATTR_REV);
	present(&g, &shown);
	CHECK(strstr(out, "\x1b[3;4H") && strstr(out, "\xc3\xa9") && !strstr(out, "hello"));
	/* grid_puts: UTF-8 by cluster, cut at end, the unprintable as blanks, the face on every cell */
	CHECK(!grid_resize(&g, 2, 16));
	const char *m = "saved \xe5\x85\xa8\xe8\xa7\x92.md";
	CHECK(grid_width(m, strlen(m)) == 13);
	CHECK(grid_puts(&g, 0, 0, g.cols, m, strlen(m), ATTR_NONE, FACE_KITTEN_TEXT) == 13);
	CHECK(g.c[6].len == 3 && !memcmp(g.c[6].ch, "\xe5\x85\xa8", 3) && !g.c[7].len && g.c[10].ch[0] == '.' && g.c[12].ch[0] == 'd');
	for (int i = 0; i < 13; i++) CHECK(g.c[i].face == FACE_KITTEN_TEXT);
	CHECK(g.c[13].ch[0] == ' ' && g.c[13].face == FACE_NONE);
	CHECK(grid_puts(&g, 1, 0, 5, "ab\xe5\x85\xa8\xe8\xa7\x92", 8, ATTR_NONE, FACE_NONE) == 4 && g.c[16 + 4].ch[0] == ' ');
	CHECK(grid_puts(&g, 1, 0, g.cols, "\x80x\t", 3, ATTR_NONE, FACE_NONE) == 6 && grid_width("\x80x\t", 3) == 6);
	for (int i = 0; i < 6; i++) CHECK(g.c[16 + i].ch[0] == (i == 4 ? 'x' : ' '));
	grid_free(&g);
	grid_free(&shown);
	puts("test_grid: identical frame sends no cells; one change sends one cell; input keeps split characters and every byte; grid_puts writes UTF-8 to an end column with a face");
	return 0;
}
