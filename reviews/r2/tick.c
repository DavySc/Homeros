/* Review of package 2, finding 3: tests/test_undo_file.c runs on the real
   clock.  Linked into that test, this clock moves one second every TICK
   calls of time(), as the real one does at some point of the test's 4 s. */
#include <time.h>
#ifndef TICK
#define TICK 997
#endif
time_t time(time_t *t)
{
	static long calls;
	time_t v = 1794925000 + calls++ / TICK;
	if (t) *t = v;
	return v;
}
