/* PR 100 review: the 'redrawtime' part of tests/test_keys_ex.c alone, with
   the number of lines as argv[1]; prints the frame's seconds (the test's
   bound is 3 s).  Build with run.sh; for "no budget" apply the plant
   keys.txt "'hlsearch' has no 'redrawtime'" to src/keys/search.c first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"

int main(int argc, char **argv)
{
	size_t n = argc > 1 ? strtoul(argv[1], NULL, 10) : 400, hs, hz;
	char path[] = "/tmp/pr100hlXXXXXX", keys[64];
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	FILE *f = fdopen(fd, "w");
	for (size_t i = 0; i < n; i++) fputs("aaaaaaaaaaaaaaaa\n", f);
	CHECK(fputs("b\n", f) >= 0 && !fclose(f));
	static Editor e;
	CHECK(!core_open(&e, path));
	snprintf(keys, sizeof keys, "%zuG/\\(a*\\)*b\r", n);
	for (char *s = keys; *s; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0);
	CHECK(e.win->line == n);
	e.win->top = 0;
	long long t0 = loop_now();
	for (size_t i = 0; i < n; i++) CHECK(!keys_hl(&e, e.win, i, 0, &hs, &hz));
	printf("%zu lines: %.3f s\n", n, (double)(loop_now() - t0) / 1e9);
	core_free(&e);
	unlink(path);
	return 0;
}
