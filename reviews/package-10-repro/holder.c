/* A child that exits while something it started still holds its stdout:
   when is the exit reported?  argv[1] = shell command. */
#include <stdio.h>
#include <time.h>
#include "loop.h"
static int done;
static long long t0;
static long long ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
static void fn(void *c, int s, const char *b, size_t n)
{
	(void)c; (void)b;
	if (s && n) printf("%5lld ms  stream %d: %zu bytes\n", ms() - t0, s, n);
	else if (s) printf("%5lld ms  stream %d ended\n", ms() - t0, s);
	else { printf("%5lld ms  exit %zu\n", ms() - t0, n); done = 1; }
}
int main(int argc, char **argv)
{
	char *sh[] = { "sh", "-c", argc > 1 ? argv[1] : "sleep 3 & echo hi", NULL };
	t0 = ms();
	proc_spawn(sh, NULL, fn, NULL);
	while (!done && ms() - t0 < 10000) loop_once(100);
	if (!done) printf("no exit reported in 10 s\n");
	return 0;
}
