/* search over 100,000 lines of 400 bytes with a pattern whose stack grows
   past 256 entries on each line: the time of 3 failing searches */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
static Editor e;
static void feed(const char *s) { for (; *s; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0); }
int main(int argc, char **argv)
{
	char path[] = "/tmp/perfXXXXXX";
	int fd = mkstemp(path);
	FILE *f = fdopen(fd, "w");
	for (int i = 0; i < 100000; i++) { for (int j = 0; j < 399; j++) fputc('a' + (i + j) % 20, f); fputc('\n', f); }
	fclose(f);
	if (core_open(&e, path)) return 1;
	long long t0 = loop_now();
	for (int i = 0; i < 3; i++) feed(argc > 1 ? argv[1] : "/.*zz\r");
	printf("%.3f s  [%s]\n", (double)(loop_now() - t0) / 1e9, e.msg);
	unlink(path);
	return 0;
}
