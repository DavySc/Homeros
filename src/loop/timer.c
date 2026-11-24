/* What is the same on both systems (loop.c, win.c): the timers, a fixed
   table, the wait cut to the first that is due, and the due ones run after
   the wait; and proc_spawn and proc_pty over each one's proc_start, and
   proc_open over proc_spawn. */
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

int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx)
{
	return proc_start(argv, dir, 0, 0, fn, ctx);
}

int proc_spawn_ov(char *const argv[], const char *dir, ProcFn fn, void *ctx)
{
	return proc_start(argv, dir, 0, -1, fn, ctx);
}

static void dropped(void *ctx, int stream, const char *b, size_t n) { (void)ctx; (void)stream; (void)b; (void)n; }

int proc_open(const char *what)
{
#ifdef _WIN32
	char *argv[] = { "explorer", (char *)what, NULL };
#else
	/* not the editor's pipes: a browser xdg-open starts itself would die
	   of SIGPIPE on its next line once the editor is gone */
	char *argv[] = { "sh", "-c", "exec xdg-open \"$0\" > /dev/null 2>&1", (char *)what, NULL };
#endif
	return proc_spawn(argv, NULL, dropped, NULL) < 0 ? -1 : 0;
}

int proc_pty(char *const argv[], const char *dir, int rows, int cols, ProcFn fn, void *ctx)
{
	return rows > 0 && cols > 0 ? proc_start(argv, dir, rows, cols, fn, ctx) : -1;
}
