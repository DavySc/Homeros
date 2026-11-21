/* Review of PR 105: loop.h says proc_wait runs "until it is reported".
   A child whose fn starts another child when it is reported: the new
   child takes the freed slot, so the id names it and the wait goes on.
   gcc -Iinclude reuse.c src/loop/loop.c src/loop/timer.c -o reuse && ./reuse */
#include <stdio.h>
#include <time.h>
#include "loop.h"
static int second = -1, ended;
static void fn2(void *c, int s, const char *b, size_t n) { (void)c; (void)s; (void)b; (void)n; }
static void fn(void *c, int s, const char *b, size_t n)
{
	(void)c; (void)b; (void)n;
	if (s) return;
	ended = 1;
	char *argv[] = { "sleep", "3", NULL };
	second = proc_spawn(argv, NULL, fn2, NULL);
}
int main(void)
{
	char *argv[] = { "true", NULL };
	int id = proc_spawn(argv, NULL, fn, NULL);
	struct timespec a, z;
	clock_gettime(CLOCK_MONOTONIC, &a);
	int r = proc_wait(id, 1000);
	clock_gettime(CLOCK_MONOTONIC, &z);
	printf("first id %d, reported %d, second id %d; proc_wait returned %d after %ld ms\n", id, ended, second, r,
	       (long)((z.tv_sec - a.tv_sec) * 1000 + (z.tv_nsec - a.tv_nsec) / 1000000));
	return 0;
}
