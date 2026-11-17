/* Re-review: why report() must clear killat.  Built against loop.c with the
   line "c->killat = 0;" removed: a killed child exits on SIGTERM, its slot
   is free (pid 0), and 2 s later the stale deadline sends
   kill(-0, SIGKILL), which is the editor's own process group.  Run under
   setsid so only this program dies. */
#include <stdio.h>
#include <time.h>
#include "loop.h"
static void fn(void *c, int s, const char *b, size_t n) { (void)c; (void)b; (void)n; if (!s) printf("child exited\n"); }
static long long ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
int main(void)
{
	char *argv[] = { "sleep", "100", NULL };
	setvbuf(stdout, NULL, _IONBF, 0);
	int id = proc_spawn(argv, NULL, fn, NULL);
	loop_once(100);
	proc_kill(id);
	for (long long t0 = ms(); ms() - t0 < 3000;) loop_once(100);
	printf("the editor is alive 3 s after proc_kill\n");
	return 0;
}
