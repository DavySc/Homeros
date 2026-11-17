/* proc_kill takes a slot in the 16-entry timer table for its SIGKILL.  N
   children that ignore SIGTERM are killed together: how many are gone 4 s
   later?  argv[1] = N (default 20). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "loop.h"
static int left;
static void fn(void *c, int s, const char *b, size_t n) { (void)c; (void)b; (void)n; if (!s) left--; }
static long long ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 20, id[32];
	char *sh[] = { "sh", "-c", "trap '' TERM; sleep 30 & wait", NULL };
	for (int i = 0; i < n; i++) id[i] = proc_spawn(sh, NULL, fn, NULL);
	left = n;
	usleep(300000);
	for (int i = 0; i < n; i++) proc_kill(id[i]);
	for (long long t0 = ms(); ms() - t0 < 4000 && left;) loop_once(100);
	printf("%d children killed, %d still alive 4 s after proc_kill\n", n, left);
	return left != 0;
}
