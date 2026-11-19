/* The timers, the same on both systems (loop.c, win.c): a fixed table, the
   wait cut to the first that is due, and the due ones run after the wait. */
#include "loop.h"
#include "timer.h"

enum { MAXT = 16 };
static struct { void (*fn)(void *); void *ctx; long long at; } t[MAXT];

int loop_timer(void (*fn)(void *), void *ctx, int ms)
{
	int f = -1;
	for (int i = 0; i < MAXT; i++) {
		if (t[i].fn == fn && t[i].ctx == ctx) { t[i].fn = NULL; f = i; break; }
		if (!t[i].fn && f < 0) f = i;
	}
	if (ms < 0) return 0;
	if (f < 0) return -1;
	t[f].fn = fn; t[f].ctx = ctx; t[f].at = loop_now() + ms * 1000000LL;
	return 0;
}

int timer_wait(long long first, int timeout_ms)
{
	for (int i = 0; i < MAXT; i++) if (t[i].fn && (first < 0 || t[i].at < first)) first = t[i].at;
	if (first < 0) return timeout_ms;
	long long d = first - loop_now();
	int ms = d <= 0 ? 0 : (int)((d + 999999) / 1000000);   /* round up: never wake before it is due */
	return timeout_ms < 0 || ms < timeout_ms ? ms : timeout_ms;
}

long long timer_run(void)
{
	long long tn = loop_now();
	for (int i = 0; i < MAXT; i++)
		if (t[i].fn && t[i].at <= tn) {
			void (*fn)(void *) = t[i].fn;
			t[i].fn = NULL;
			fn(t[i].ctx);
		}
	return tn;
}
