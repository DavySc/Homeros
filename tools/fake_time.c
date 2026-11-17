/* LD_PRELOAD into nvim: time() reads HOMEROS_TIME, which the test sets, so
   Neovim's undo timestamps follow the test's clock. */
#include <stdlib.h>
#include <time.h>
time_t time(time_t *t)
{
	const char *s = getenv("HOMEROS_TIME");
	time_t v = s ? (time_t)atoll(s) : 0;
	if (t) *t = v;
	return v;
}
