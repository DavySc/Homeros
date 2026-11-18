/* Review of PR 41: probes of src/loop/win.c through loop.h, run under Wine
   (run.sh).  Each probe is named by argv[1]; the children are this program
   again, in a role named "c-<role>".
     flood   a child that writes without end: how long is one loop_once(0),
             how many bytes does it hand on, how late is a key
     drain   the exit is seen while the child's last output is still in the
             pipe (a watch function sleeps over the child's whole life)
     early   a callback inside proc_spawn (built with -DSPAWN_SLEEP, a
             stand-in for the editor's thread losing the CPU for 300 ms)
     stale   a proc_write from the callback inside the exit's drain, to a
             child whose stdin a grandchild still holds; then the slot's
             next child */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"

static char self[MAX_PATH];
static long long us(void)
{
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return c.QuadPart * 1000000 / f.QuadPart;
}

static int child(const char *r)
{
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
	static char b[1 << 20];
	DWORD n;
	if (!strcmp(r, "c-flood")) { memset(b, 'f', sizeof b); while (WriteFile(out, b, sizeof b, &n, NULL)) {} return 0; }
	if (!strcmp(r, "c-quick")) return 0;
	if (!strcmp(r, "c-say")) { WriteFile(out, "hello\n", 6, &n, NULL); return 7; }
	if (!strcmp(r, "c-sleep")) { Sleep(4000); return 0; }
	if (!strcmp(r, "c-cat")) { while (ReadFile(in, b, sizeof b, &n, NULL) && n) if (!WriteFile(out, b, n, &n, NULL)) return 1; return 0; }
	if (!strcmp(r, "c-holdin")) {   /* leaves a grandchild holding all three pipes, asks, exits */
		char line[MAX_PATH + 32];
		STARTUPINFOA si = { .cb = sizeof si };
		PROCESS_INFORMATION pi;
		snprintf(line, sizeof line, "\"%s\" c-sleep", self);
		if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 9;
		WriteFile(out, "req\n", 4, &n, NULL);
		return 0;
	}
	return 99;
}

static int spawn(const char *role, ProcFn fn, void *ctx)
{
	char *argv[] = { self, (char *)role, NULL };
	return proc_spawn(argv, NULL, fn, ctx);
}

/* --- flood --- */
static long long bytes, key_at;
static int done;
static void on_flood(void *ctx, int s, const char *b, size_t n) { (void)ctx; (void)b; if (s == 1) bytes += (long long)n; if (!s) done = 1; }
static void on_key(void *ctx, int fd) { (void)ctx; (void)fd; if (!key_at) key_at = us(); }

static int flood(void)
{
	LARGE_INTEGER due = { .QuadPart = -300 * 10000LL };
	HANDLE key = CreateWaitableTimerW(NULL, FALSE, NULL);
	loop_watch((int)(INT_PTR)key, on_key, NULL);
	int id = spawn("c-flood", on_flood, NULL);
	if (id < 0) return 1;
	long long t0 = us(), worst = 0, most = 0, calls = 0;
	SetWaitableTimer(key, &due, 0, NULL, NULL, FALSE);
	while (us() - t0 < 2000000) {
		long long a = us(), b0 = bytes;
		loop_once(0);
		long long d = us() - a;
		if (d > worst) worst = d;
		if (bytes - b0 > most) most = bytes - b0;
		calls++;
	}
	printf("flood: %lld loop_once(0) calls in 2 s, %lld MB read; longest call %lld ms, most bytes in one call %lld; key due at 300 ms ran at %lld ms\n",
	       calls, bytes >> 20, worst / 1000, most, key_at ? (key_at - t0) / 1000 : -1);
	proc_kill(id);
	a: if (!done) { loop_once(100); goto a; }
	return 0;
}

/* --- drain --- */
static char got[64];
static size_t ngot;
static int ended[3], code = -1, in_spawn, early;
static void on_say(void *ctx, int s, const char *b, size_t n)
{
	(void)ctx;
	if (in_spawn) early++;
	if (!s) { ended[0] = 1; code = (int)n; return; }
	if (!n) { ended[s] = 1; return; }
	if (s == 1 && ngot + n <= sizeof got) { memcpy(got + ngot, b, n); ngot += n; }
}
static void on_slow(void *ctx, int fd) { (void)ctx; (void)fd; Sleep(1500); }

static int drain(void)
{
	HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
	loop_watch((int)(INT_PTR)e, on_slow, NULL);
	if (spawn("c-say", on_say, NULL) < 0) return 1;
	SetEvent(e);
	loop_once(-1);   /* the watch sleeps 1.5 s; the child writes and exits meanwhile */
	int one = ended[0];
	for (int i = 0; i < 50 && !ended[0]; i++) loop_once(100);
	printf("drain: exit reported in the first loop_once: %d; code %d, output %zu bytes \"%.*s\", stream ends %d %d\n",
	       one, code, ngot, (int)(ngot ? ngot - 1 : 0), got, ended[1], ended[2]);
	return !(one && code == 7 && ngot == 6 && !memcmp(got, "hello\n", 6) && ended[1] && ended[2]);
}

/* --- early --- */
static int early_cb(void)
{
	in_spawn = 1;
	int id = spawn("c-quick", on_say, NULL);
	in_spawn = 0;
	for (int i = 0; i < 50 && !ended[0]; i++) loop_once(100);
	printf("early: proc_spawn returned %d; callbacks that ran before it returned: %d\n", id, early);
	return early != 0;
}

/* --- stale --- */
static int id1 = -1, wrote = -2;
static char mb[1 << 20];
static void on_req(void *ctx, int s, const char *b, size_t n)
{
	(void)ctx; (void)b;
	if (s == 1 && n && wrote == -2) wrote = proc_write(id1, mb, sizeof mb);   /* the answer to "req" */
	if (!s) ended[0] = 1;
}
static char back[64];
static size_t nback, total;
static int cat_done, cat_code = -1;
static void on_cat(void *ctx, int s, const char *b, size_t n)
{
	(void)ctx;
	if (s == 1) total += n;
	if (s == 1 && n && nback + n <= sizeof back) { memcpy(back + nback, b, n); nback += n; }
	if (!s) { cat_done = 1; cat_code = (int)n; }
}

static int stale(void)
{
	HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
	loop_watch((int)(INT_PTR)e, on_slow, NULL);
	id1 = spawn("c-holdin", on_req, NULL);
	if (id1 < 0) return 1;
	SetEvent(e);
	loop_once(-1);   /* "req" is still in the pipe when the exit is seen */
	loop_unwatch((int)(INT_PTR)e);
	printf("stale: exit reported %d, proc_write inside the drain returned %d\n", ended[0], wrote);
	int id2 = spawn("c-cat", on_cat, NULL);
	printf("stale: the next child got id %d (the first had %d)\n", id2, id1);
	int w = proc_write(id2, "abc", 3);
	long long t0 = us();
	for (int i = 0; i < 5; i++) loop_once(100);
	int w2 = proc_write(id2, "def", 3);   /* -1: the first child's write completion closed this child's stdin */
	proc_close(id2);
	while (!cat_done && us() - t0 < 6000000) loop_once(100);
	printf("stale: the next child is cat: proc_write \"abc\" returned %d, proc_write \"def\" 500 ms later returned %d; cat sent back %zu bytes \"%.*s\", exit %s code %d\n",
	       w, w2, total, (int)nback, back, cat_done ? "reported" : "NOT reported in 6 s", cat_code);
	return !(cat_done && !w && !w2 && total == 6 && !memcmp(back, "abcdef", 6));
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	GetModuleFileNameA(NULL, self, sizeof self);
	if (argc < 2) return 2;
	if (!strncmp(argv[1], "c-", 2)) return child(argv[1]);
	if (!strcmp(argv[1], "flood")) return flood();
	if (!strcmp(argv[1], "drain")) return drain();
	if (!strcmp(argv[1], "early")) return early_cb();
	if (!strcmp(argv[1], "stale")) return stale();
	return 2;
}
