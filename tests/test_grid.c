/* The terminal backend sends only changed cells: present a frame, present it
   again (no cell output), change one cell (exactly that cell is sent). */
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
	backend_present(g, shown);
	dup2(saved, 1);
	close(p[1]);
	close(saved);
	ssize_t n = read(p[0], out, sizeof out - 1);
	close(p[0]);
	CHECK(n > 0);
	out[n] = 0;
	return (size_t)n;
}

int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	CHECK(!grid_resize(&g, 4, 10));
	grid_puts(&g, 0, 0, "hello", ATTR_NONE);
	present(&g, &shown);
	CHECK(strstr(out, "hello") && strstr(out, "\x1b[?2026h") && strstr(out, "\x1b[?2026l"));
	size_t idle = present(&g, &shown);
	CHECK(!strstr(out, "hello") && idle < 40);          /* only the wrapper and cursor */
	grid_put(&g, 2, 3, (const unsigned char *)"\xc3\xa9", 2, 1, ATTR_REV);
	present(&g, &shown);
	CHECK(strstr(out, "\x1b[3;4H") && strstr(out, "\xc3\xa9") && !strstr(out, "hello"));
	grid_free(&g);
	grid_free(&shown);
	puts("test_grid: identical frame sends no cells; one change sends one cell");
	return 0;
}
