/* The pickers end to end: a scratch tree, the real loop and rg, keys typed
   into the picker, the list read back from the drawn grid.  The orders are
   snacks.picker's own on the same tree (src/tools/conform/pick.lua). */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "loop.h"
#include "tools.h"
static Editor e;
static Grid g;

/* every path has its own length, so rg's order (it lists in parallel) never
   decides a tie */
static const char *tree[] = {
	"Makefile", "README.md", "picker.lua", "docs/READ.md", "src/core/core.c", "src/core/coredd.hpp",
	"src/tools/pick.c", "tests/test_pick.c", "src/tools/match.hh", "src/tools/matchx.ccc", ".hidden/x.c", NULL
};

static long long ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* until 300 ms pass with nothing for the picker: the finder is done (a
   killed finder's end wakes the loop too, so the wait is timed) */
static void settle(void)
{
	long long t0 = ms();
	while (ms() - t0 < 300) {
		core_changed = 0;
		CHECK(!loop_once(300));
		if (core_changed) t0 = ms();
	}
}

static void type(const char *s)
{
	for (; *s; s++) CHECK(tools_key(&e, (unsigned char)*s));
	settle();
}

/* The list as drawn: one name per row, the selected row first if sel. */
static void rows(char *out, size_t cap)
{
	core_draw(&e, &g);
	tools_draw(&e, &g);
	size_t n = 0;
	out[0] = 0;
	for (int r = 0; r < g.rows; r++) {
		char line[1024];
		size_t k = 0;
		for (int c = 0; c < g.cols && k + 4 < sizeof line; c++) {
			Cell *x = &g.c[r * g.cols + c];
			memcpy(line + k, x->ch, x->len); k += x->len;
		}
		line[k] = 0;
		/* a list row is "│ path ││preview│": take what is between the first two bars */
		char *s = strstr(line, "\xe2\x94\x82"), *z;
		if (strstr(line, "\xe2\x94\x9c")) continue;     /* the prompt's underline */
		if (!s || !(z = strstr(s + 3, "\xe2\x94\x82"))) continue;
		s += 3; *z = 0;
		while (*s == ' ') s++;
		while (z > s && z[-1] == ' ') *--z = 0;
		if (!*s || strstr(s, "> ") == s) continue;
		if (n + strlen(s) + 2 < cap) { n += (size_t)snprintf(out + n, cap - n, "%s|", s); }
	}
}

static void expect(const char *prompt, const char *want)
{
	char got[4096];
	CHECK(!tools_pick(&e, PICK_SMART));
	type(prompt);
	rows(got, sizeof got);
	if (getenv("SHOW")) for (int r = 0; r < g.rows; r++) { for (int c = 0; c < g.cols; c++) { Cell *x = &g.c[r * g.cols + c]; fwrite(x->ch, 1, x->len, stderr); } fputc('\n', stderr); }
	if (strcmp(got, want)) { fprintf(stderr, "prompt \"%s\"\n  snacks %s\n  ours   %s\n", prompt, want, got); exit(1); }
	tools_key(&e, KEY_ESC); tools_key(&e, KEY_ESC);
}

static int by_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Live grep: rows "path:line:col:text" cut to "path:line", sorted (rg
   lists files in parallel, and so in no fixed order). */
static void expect_grep(const char *prompt, const char *want)
{
	char got[4096], out[4096], *row[64];
	int n = 0;
	CHECK(!tools_pick(&e, PICK_GREP));
	type(prompt);
	rows(got, sizeof got);
	for (char *s = got, *z; (z = strchr(s, '|')); s = z + 1) {
		*z = 0;
		char *c = strchr(s, ':');
		if (c && (c = strchr(c + 1, ':'))) *c = 0;
		CHECK(n < 64);
		row[n++] = s;
	}
	qsort(row, (size_t)n, sizeof *row, by_str);
	size_t k = 0;
	out[0] = 0;
	for (int i = 0; i < n; i++) k += (size_t)snprintf(out + k, sizeof out - k, "%s|", row[i]);
	if (strcmp(out, want) && getenv("SHOW")) for (int r = 0; r < g.rows; r++) { for (int c = 0; c < g.cols; c++) { Cell *x = &g.c[r * g.cols + c]; fwrite(x->ch, 1, x->len, stderr); } fputc(10, stderr); }
	if (strcmp(out, want)) { fprintf(stderr, "grep \"%s\"\n  snacks %s\n  ours   %s\n", prompt, want, out); exit(1); }
	tools_key(&e, KEY_ESC); tools_key(&e, KEY_ESC);
}

int main(void)
{
	/* the buffer's text holds the full path, so the tree is where snacks'
	   was when it was recorded */
	const char *dir = "/tmp/hp";
	CHECK(!system("rm -rf /tmp/hp /tmp/hp-data") && !mkdir(dir, 0700) && !chdir(dir));
	/* no frecency history: the picker's store goes in the scratch tree */
	char data[64];
	snprintf(data, sizeof data, "%s-data", dir);
	setenv("XDG_DATA_HOME", data, 1);
	for (const char **p = tree; *p; p++) {
		char d[256];
		strcpy(d, *p);
		for (char *s = strchr(d, '/'); s; s = strchr(s + 1, '/')) { *s = 0; mkdir(d, 0700); *s = '/'; }
		FILE *f = fopen(*p, "w");
		CHECK(f);
		fputs("line one\nint pick(void) { return 1; }\nlast line\n", f);
		fclose(f);
	}
	CHECK(!core_open(&e, "src/core/core.c"));
	CHECK(!grid_resize(&g, 40, 140));
	e.rows = 40; e.cols = 140;
#include "../src/tools/conform/pick-expect.inc"
	char rm[160];
	snprintf(rm, sizeof rm, "rm -rf %s %s", dir, data);
	CHECK(!system(rm));
	core_free(&e);
	grid_free(&g);
	printf("test_tools_pick: ok\n");
	return 0;
}
