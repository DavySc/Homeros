/* src/loop/ on Windows (win.c), black box through loop.h, the twin of
   test_loop.c.  The children are this program run again in a role named by
   its first argument, since Wine has no sh, cat or sleep worth trusting.
   1. Pipes: 4 MB through cat comes back byte for byte while the writes
      return at once (a child that writes before it reads cannot deadlock
      the editor: dead.py); stdout and stderr apart; exit codes after all
      output; a bad program is -1; the working directory; a child that
      closes its stdin; 32 children at once, a 33rd refused.
   2. proc_kill ends the child and what it started, with 128 + SIGTERM.
   3. The exit is reported when the child exits, though a process it left
      holds the pipes, which are closed by then.
   4. Timers; watches on handles and on the message queue (also a message
      seen but left queued); every ready watch runs, and an auto-reset
      handle whose signal loop_waiting took still runs its function.
   5. Ctrl-C inside an endless command; pipes and timers wait until it is
      over.  6. No return from an idle loop_once before its timeout.
   7. From the review of PR 41: a child that never stops writing does not
      hold loop_once; output still in the pipe at the exit is read first; a
      write begun inside the exit's drain is refused.
   8. loop.h's write wake: a socket's WSAEventSelect event wakes its watch
      once the peer reads after a send would block, and not while it is full.
   9. proc_pty: the child's console is the pseudoconsole, at the size
      asked; what is typed reaches it; a resize reaches it (on Windows:
      Wine does not resize one).
   Runs on Windows or under Wine (src/loop/check.sh); elsewhere it says so. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "loop.h"

static char self[MAX_PATH];

/* the loop's own clock: GetTickCount64 steps by 15.6 ms on Windows (1 ms
   under Wine), so on Windows a timer due in 200 ms showed under 195 */
static long long ms(void) { return loop_now() / 1000000; }

/* --- the children --- */

static void put(HANDLE h, const char *s) { DWORD n; WriteFile(h, s, (DWORD)strlen(s), &n, NULL); }

static DWORD start(const char *role, int inherit)
{
	char line[MAX_PATH + 32];
	STARTUPINFOA si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	snprintf(line, sizeof line, "\"%s\" %s", self, role);
	if (inherit) {   /* it holds this one's stdout and stderr */
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	}
	if (!CreateProcessA(NULL, line, NULL, NULL, inherit, 0, NULL, NULL, &si, &pi)) return 0;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return pi.dwProcessId;
}

static int child(char **argv)
{
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
	static char b[1 << 16];
	DWORD n;
	const char *r = argv[1];
	if (!strcmp(r, "cat")) {
		while (ReadFile(in, b, sizeof b, &n, NULL) && n) if (!WriteFile(out, b, n, &n, NULL)) return 1;
		return 0;
	}
	if (!strcmp(r, "first")) {   /* writes 200 KB before it reads (dead.py) */
		memset(b, 'z', sizeof b);
		for (int k = 0; k < 200000; k += 50000) WriteFile(out, b, 50000, &n, NULL);
		while (ReadFile(in, b, sizeof b, &n, NULL) && n) {}
		put(err, "done\n");
		return 0;
	}
	if (!strcmp(r, "streams")) { put(out, "out\n"); put(err, "err\n"); return 3; }
	if (!strcmp(r, "args")) {   /* the argv it got, one per line, in UTF-8 (argv is the ANSI code page's) */
		int wn;
		wchar_t **wa = CommandLineToArgvW(GetCommandLineW(), &wn);
		for (int i = 2; i < wn; i++) {
			WideCharToMultiByte(CP_UTF8, 0, wa[i], -1, b, sizeof b, NULL, NULL);
			put(out, b);
			put(out, "\n");
		}
		return 0;
	}
	if (!strcmp(r, "pwd")) { GetCurrentDirectoryA(sizeof b, b); put(out, b); return 0; }
	if (!strcmp(r, "head1")) { ReadFile(in, b, 1, &n, NULL); return 0; }
	if (!strcmp(r, "noin")) { Sleep(200); CloseHandle(in); Sleep(1000); return 0; }
	if (!strcmp(r, "holdw")) {   /* 0 if either pipe still takes a byte after 1 s */
		Sleep(1000);
		return !(WriteFile(out, "x", 1, &n, NULL) | WriteFile(err, "x", 1, &n, NULL));
	}
	if (!strcmp(r, "slowcat")) { Sleep(200); argv[1] = "cat"; return child(argv); }
	if (!strcmp(r, "sleep")) { Sleep(30000); return 0; }
	if (!strcmp(r, "flood")) { memset(b, 'f', sizeof b); while (WriteFile(out, b, sizeof b, &n, NULL)) {} return 0; }
	if (!strcmp(r, "say")) {   /* holds waits()'s mutex, if there is one, until it exits */
		WaitForSingleObject(OpenMutexA(SYNCHRONIZE, FALSE, getenv("HOMEROS_SAY")), INFINITE);
		put(out, "hello\n");
		return 7;
	}
	if (!strcmp(r, "holdin")) {   /* a grandchild holds all its pipes, stdin too; asks with its pid */
		snprintf(b, sizeof b, "%lu\n", start("sleep", 1));
		put(out, b);
		return 0;
	}
	if (!strcmp(r, "tree")) {   /* starts a grandchild, says its pid, waits */
		snprintf(b, sizeof b, "%lu\n", start("sleep", 0));
		put(out, b);
		Sleep(30000);
		return 0;
	}
	if (!strcmp(r, "hold")) {   /* leaves a grandchild holding its pipes */
		snprintf(b, sizeof b, "%lu\n", start("holdw", 1));
		put(out, b);
		return 0;
	}
	if (!strcmp(r, "big")) {   /* 1 MB on each stream, interleaved, then exits */
		for (int k = 0; k < 16; k++) {
			memset(b, 'a' + k, sizeof b);
			WriteFile(out, b, sizeof b, &n, NULL);
			WriteFile(err, b, sizeof b, &n, NULL);
		}
		return 0;
	}
	if (!strcmp(r, "console")) {   /* its console's size, a line typed, the size after a resize, a line */
		CONSOLE_SCREEN_BUFFER_INFO ci, now;
		if (!GetConsoleScreenBufferInfo(out, &ci)) return 98;
		snprintf(b, sizeof b, "size %d %d\n", ci.srWindow.Bottom - ci.srWindow.Top + 1, ci.srWindow.Right - ci.srWindow.Left + 1);
		put(out, b);
		if (!ReadFile(in, b, 64, &n, NULL)) return 97;
		put(out, n >= 2 && !memcmp(b, "hi", 2) ? "got:hi\n" : "got:?\n");
		for (int k = 0; k < 200 && GetConsoleScreenBufferInfo(out, &now) && now.srWindow.Right == ci.srWindow.Right; k++) Sleep(10);
		snprintf(b, sizeof b, "now %d %d\n", now.srWindow.Bottom - now.srWindow.Top + 1, now.srWindow.Right - now.srWindow.Left + 1);
		put(out, b);
		/* a line before exiting: Windows' ConPTY drops what a child writes
		   just before it exits (seen on CI's windows job, 1 run) */
		return ReadFile(in, b, 64, &n, NULL) ? 3 : 96;
	}
	return 99;
}

/* --- the editor's side --- */

typedef struct {
	char *out; size_t nout, cap;
	char err[1 << 21]; size_t nerr;
	int ended[3], code, order;
} Got;

static void on_proc(void *ctx, int s, const char *b, size_t n)
{
	Got *g = ctx;
	CHECK(!g->ended[0]);
	if (s == 0) { g->ended[0] = 1; g->code = (int)n; g->order = g->ended[1] && g->ended[2]; return; }
	if (!n) { CHECK(!g->ended[s]); g->ended[s] = 1; return; }
	CHECK(!g->ended[s] && b);
	if (s == 2) { CHECK(g->nerr + n <= sizeof g->err); memcpy(g->err + g->nerr, b, n); g->nerr += n; return; }
	if (g->nout + n > g->cap) {
		g->cap = (g->nout + n) * 2;
		g->out = realloc(g->out, g->cap);
		CHECK(g->out);
	}
	memcpy(g->out + g->nout, b, n);
	g->nout += n;
}

static void run(Got *g, long long limit)
{
	long long end = ms() + limit;
	while (!g->ended[0]) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	CHECK(g->order);
}

static int spawn(Got *g, const char *role, const char *dir)
{
	char *argv[] = { self, (char *)role, NULL };
	memset(g, 0, sizeof *g);
	return proc_spawn(argv, dir, on_proc, g);
}

static Got g, gs[33];

static int has(const char *s)   /* mingw has no memmem */
{
	size_t k = strlen(s);
	for (size_t i = 0; i + k <= g.nout; i++) if (!memcmp(g.out + i, s, k)) return 1;
	return 0;
}

static void pty(void)
{
	char *argv[] = { self, "console", NULL };
	memset(&g, 0, sizeof g);
	int id = proc_pty(argv, NULL, 7, 33, on_proc, &g);
	CHECK(id >= 0);
	long long end = ms() + 20000;
	while (!has("size 7 33")) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	CHECK(proc_write(id, "hi\r", 3) == 0);
	while (!has("got:")) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	proc_resize(id, 9, 41);
	while (!has("now ")) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	CHECK(proc_write(id, "\r", 1) == 0);
	while (!g.ended[0]) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	/* Wine 11's ResizePseudoConsole is a stub (E_NOTIMPL), so there the
	   size stays */
	int wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
	if (!(g.code == 3 && g.ended[1] && !g.ended[2] && has("got:hi") && has(wine ? "now 7 33" : "now 9 41"))) {
		fprintf(stderr, "pty: code %d, ended %d %d %d, output:\n", g.code, g.ended[0], g.ended[1], g.ended[2]);
		for (size_t i = 0; i < g.nout; i++) fprintf(stderr, g.out[i] >= ' ' && g.out[i] < 127 ? "%c" : "\\x%02x", (unsigned char)g.out[i]);
		CHECK(0);
	}
	free(g.out);
}

static void pipes(void)
{
	enum { N = 4 << 20 };
	static char in[N], mb[1 << 20];
	for (int i = 0; i < N; i++) in[i] = (char)rnd(256);
	int id = spawn(&g, "cat", NULL);
	CHECK(id >= 0);
	long long t0 = ms();
	for (size_t off = 0, k; off < N; off += k) {
		k = 1 + rnd(200000);
		if (k > N - off) k = N - off;
		CHECK(proc_write(id, in + off, k) == 0);
		if (rnd(2)) CHECK(loop_once(0) == 0);
	}
	CHECK(ms() - t0 < 1000);   /* queued, not waited for */
	proc_close(id);
	CHECK(proc_write(id, "x", 1) == -1);
	run(&g, 20000);
	CHECK(g.code == 0 && g.nout == N && !memcmp(g.out, in, N) && !g.nerr);
	free(g.out);

	id = spawn(&g, "first", NULL);
	CHECK(id >= 0);
	t0 = ms();
	CHECK(proc_write(id, mb, sizeof mb) == 0);
	CHECK(ms() - t0 < 100);
	proc_close(id);
	run(&g, 10000);
	CHECK(g.code == 0 && g.nout == 200000 && g.nerr == 5 && !memcmp(g.err, "done\n", 5));
	free(g.out);

	CHECK(spawn(&g, "streams", NULL) >= 0);
	run(&g, 5000);
	CHECK(g.code == 3 && g.nout == 4 && !memcmp(g.out, "out\n", 4) && g.nerr == 4 && !memcmp(g.err, "err\n", 4));
	free(g.out);

	CHECK(spawn(&g, "big", NULL) >= 0);
	run(&g, 10000);
	CHECK(g.code == 0 && g.nout == 16 << 16 && g.nerr == 16 << 16);
	for (int k = 0; k < 16; k++) CHECK(g.out[k << 16] == 'a' + k && g.err[(k << 16) + 65535] == 'a' + k);
	free(g.out);

	/* arguments with blanks, quotes and backslashes arrive as they were */
	char *argv[] = { self, "args", "a b", "\"q\"", "c:\\x\\", "d\\\\\"e", "", "ü", NULL };
	memset(&g, 0, sizeof g);
	CHECK(proc_spawn(argv, NULL, on_proc, &g) >= 0);
	run(&g, 5000);
	static const char want[] = "a b\n\"q\"\nc:\\x\\\nd\\\\\"e\n\n\xc3\xbc\n";
	CHECK(g.code == 0 && g.nout == sizeof want - 1 && !memcmp(g.out, want, sizeof want - 1));
	free(g.out);

	CHECK((id = spawn(&g, "cat", NULL)) >= 0 && proc_write(id, NULL, 0) == 0);
	proc_close(id);
	run(&g, 5000);
	CHECK(g.code == 0 && !g.nout);

	char *bad[] = { "c:\\nonexistent\\homeros-no-such-program.exe", NULL };
	CHECK(proc_spawn(bad, NULL, on_proc, &g) == -1);

	CHECK(spawn(&g, "pwd", "C:\\windows") >= 0);
	run(&g, 5000);
	CHECK(g.code == 0 && g.nout == 10 && !_strnicmp(g.out, "C:\\windows", 10));
	free(g.out);

	/* the rest of the write meets a closed pipe: the editor goes on */
	CHECK((id = spawn(&g, "head1", NULL)) >= 0 && proc_write(id, mb, sizeof mb) == 0);
	run(&g, 5000);
	CHECK(g.code == 0 && proc_write(id, "x", 1) == -1);

	CHECK((id = spawn(&g, "noin", NULL)) >= 0 && proc_write(id, mb, sizeof mb) == 0);
	int passes = 0;
	while (!g.ended[0]) { CHECK(loop_once(5000) == 0); passes++; }
	CHECK(passes < 10 && g.code == 0);

	for (int i = 0; i < 32; i++) {
		CHECK((id = spawn(&gs[i], "cat", NULL)) >= 0);
		CHECK(proc_write(id, mb, 256 << 10) == 0);
		proc_close(id);
	}
	CHECK(spawn(&gs[32], "cat", NULL) == -1);   /* full */
	for (int i = 0; i < 32; i++) { run(&gs[i], 20000); CHECK(gs[i].code == 0 && gs[i].nout == 256 << 10); free(gs[i].out); }
}

static int alive(DWORD pid)
{
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!h) return 0;
	int r = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
	CloseHandle(h);
	return r;
}

static DWORD said_pid(Got *x)
{
	while (!x->nout || x->out[x->nout - 1] != '\n') CHECK(loop_once(1000) == 0);
	return (DWORD)atol(x->out);
}

static void kills(void)
{
	int id = spawn(&g, "tree", NULL);
	CHECK(id >= 0);
	DWORD gc = said_pid(&g);
	CHECK(gc && alive(gc));
	long long t0 = ms();
	proc_kill(id);
	run(&g, 1000);
	CHECK(g.code == 128 + 15 && ms() - t0 < 500);
	Sleep(100);
	CHECK(!alive(gc));                       /* what it started died with it */
	free(g.out);
	proc_kill(id);                           /* reported: nothing to kill */
}

static void holder(void)
{
	long long t0 = ms();
	CHECK(spawn(&g, "hold", NULL) >= 0);
	DWORD gc = said_pid(&g);
	run(&g, 2000);
	CHECK(g.code == 0 && ms() - t0 < 1500 && alive(gc));
	/* the editor closed its ends at the exit: the holder's writes fail
	   (GetProcessHandleCount, which would count them, is 0 under Wine) */
	HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, gc);
	DWORD code = 0;
	CHECK(h && WaitForSingleObject(h, 3000) == WAIT_OBJECT_0 && GetExitCodeProcess(h, &code) && code == 1);
	CloseHandle(h);
	free(g.out);
}

static int fired[5];
static void tick(void *ctx) { fired[(int *)ctx - fired]++; }

static void timers(void)
{
	long long t0 = ms();
	CHECK(loop_timer(tick, &fired[0], 300) == 0);
	CHECK(loop_timer(tick, &fired[1], 100) == 0);
	CHECK(loop_timer(tick, &fired[2], 50) == 0);
	CHECK(loop_timer(tick, &fired[1], 200) == 0);
	CHECK(loop_timer(tick, &fired[2], -1) == 0);
	while (!fired[1]) CHECK(loop_once(-1) == 0);
	CHECK(!fired[0] && ms() - t0 >= 199);
	while (!fired[0]) CHECK(loop_once(-1) == 0);
	CHECK(fired[1] == 1 && !fired[2] && ms() - t0 >= 299 && ms() - t0 < 450);
	static char slot[17];
	for (int i = 0; i < 16; i++) CHECK(loop_timer(tick, &slot[i], 1000) == 0);
	CHECK(loop_timer(tick, &slot[16], 1000) == -1);
	CHECK(loop_timer(tick, &slot[3], 500) == 0);
	for (int i = 0; i < 16; i++) CHECK(loop_timer(tick, &slot[i], -1) == 0);
}

static int ran[3];
static HANDLE ev[3];
static void on_ev(void *ctx, int fd) { (void)fd; ran[(int)(INT_PTR)ctx]++; }
static void on_ev0_unwatch(void *ctx, int fd) { on_ev(ctx, fd); loop_unwatch((int)(INT_PTR)ev[1]); }
static void on_msg(void *ctx, int fd)
{
	MSG m;
	(void)fd;
	while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) if (m.message == WM_USER) ran[(int)(INT_PTR)ctx]++;
}

static void watches(void)
{
	for (int i = 0; i < 3; i++) CHECK((ev[i] = CreateEventW(NULL, FALSE, FALSE, NULL)) != NULL);   /* auto-reset */
	/* every ready watch runs, not only the one the wait names */
	CHECK(loop_watch((int)(INT_PTR)ev[0], on_ev, (void *)0) == 0 && loop_watch((int)(INT_PTR)ev[1], on_ev, (void *)1) == 0);
	SetEvent(ev[0]); SetEvent(ev[1]);
	CHECK(loop_once(1000) == 0 && ran[0] == 1 && ran[1] == 1);
	/* loop_waiting takes an auto-reset signal and runs nothing; the next
	   loop_once still runs the function */
	SetEvent(ev[1]);
	CHECK(loop_waiting() && loop_waiting() && ran[1] == 1);
	long long t0 = ms();
	CHECK(loop_once(1000) == 0 && ran[1] == 2 && ms() - t0 < 500);
	/* a function that removes another watch stops it, in both passes */
	loop_unwatch((int)(INT_PTR)ev[0]);
	CHECK(loop_watch((int)(INT_PTR)ev[0], on_ev0_unwatch, (void *)0) == 0);
	SetEvent(ev[0]); SetEvent(ev[1]);
	CHECK(loop_once(1000) == 0 && ran[0] == 2 && ran[1] == 2);
	CHECK(loop_watch((int)(INT_PTR)ev[1], on_ev, (void *)1) == 0);
	SetEvent(ev[0]); SetEvent(ev[1]);
	for (int i = 0; i < 32; i++) loop_breakcheck();
	CHECK(ran[0] == 3 && ran[1] == 2);
	loop_unwatch((int)(INT_PTR)ev[0]);
	ResetEvent(ev[1]);
	loop_unwatch((int)(INT_PTR)ev[1]);
	/* the message queue, also a message seen but left queued */
	CHECK(loop_watch(LOOP_MSGQ, on_msg, (void *)2) == 0);
	PostThreadMessageW(GetCurrentThreadId(), WM_USER, 0, 0);
	CHECK(loop_once(1000) == 0 && ran[2] == 1);
	MSG m;
	PostThreadMessageW(GetCurrentThreadId(), WM_USER, 0, 0);
	CHECK(PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE));
	t0 = ms();
	CHECK(loop_once(1000) == 0 && ran[2] == 2 && ms() - t0 < 500);
	loop_unwatch(LOOP_MSGQ);
}

/* Ctrl-C arrives on a timer handle 300 ms into an endless command. */
static HANDLE key;
static void on_key(void *ctx, int fd) { (void)ctx; (void)fd; loop_int = 1; }

static void ctrl_c(void)
{
	LARGE_INTEGER due = { .QuadPart = -300 * 10000LL };
	CHECK((key = CreateWaitableTimerW(NULL, FALSE, NULL)) != NULL);
	CHECK(loop_watch((int)(INT_PTR)key, on_key, NULL) == 0);
	CHECK(!loop_waiting());
	CHECK(spawn(&g, "streams", NULL) >= 0);
	CHECK(loop_once(0) == 0);                /* its reads are armed, so their completions wait in the queue */
	CHECK(loop_timer(tick, &fired[3], 0) == 0);
	Sleep(300);                              /* the child has written and exited */
	CHECK(SetWaitableTimer(key, &due, 0, NULL, NULL, FALSE));
	long long t0 = ms(), calls = 0;
	volatile unsigned long long work = 0;
	while (!loop_breakcheck() && ms() - t0 < 2000) { for (int i = 0; i < 1000; i++) work += (unsigned)i; calls++; }
	long long took = ms() - t0;
	CHECK(took >= 290 && took < 400);
	CHECK(!fired[3] && !g.nout && !g.ended[1]);   /* nothing ran inside the command */
	fprintf(stderr, "test_loop_win (measured): Ctrl-C stopped the command %lld ms after the start (key due at 300 ms), %lld breakchecks\n", took, calls);
	loop_int = 0;
	run(&g, 5000);
	CHECK(fired[3] == 1 && g.code == 3);
	free(g.out);
	loop_unwatch((int)(INT_PTR)key);
}

/* Idle with a child, a watch and a timer far ahead, loop_once returns at
   its timeout and not before. */
/* A child that never stops writing: one loop_once hands on one buffer per
   stream, as loop.c's one read per poll, and a key is not kept waiting
   (review of PR 41, 1: completion routines that re-armed their reads held
   one loop_once(0) for 1.9 s). */
static long long flooded;
static void on_flood(void *ctx, int s, const char *b, size_t n) { (void)b; if (s == 1) flooded += (long long)n; if (!s) *(int *)ctx = 1; }
static long long key_at;
static void on_key_at(void *ctx, int fd) { (void)ctx; (void)fd; if (!key_at) key_at = ms(); }

static void flood(void)
{
	LARGE_INTEGER due = { .QuadPart = -300 * 10000LL };
	HANDLE k = CreateWaitableTimerW(NULL, FALSE, NULL);
	int done = 0;
	char *argv[] = { self, "flood", NULL };
	CHECK(k && loop_watch((int)(INT_PTR)k, on_key_at, NULL) == 0);
	int id = proc_spawn(argv, NULL, on_flood, &done);
	CHECK(id >= 0);
	long long t0 = ms(), worst = 0, most = 0;
	CHECK(SetWaitableTimer(k, &due, 0, NULL, NULL, FALSE));
	while (ms() - t0 < 1000) {
		long long a = ms(), b0 = flooded;
		CHECK(loop_once(0) == 0);
		if (ms() - a > worst) worst = ms() - a;
		if (flooded - b0 > most) most = flooded - b0;
	}
	fprintf(stderr, "test_loop_win (measured): flood: longest loop_once(0) %lld ms, at most %lld bytes in one, key due at 300 ms ran at %lld ms\n", worst, most, key_at - t0);
	CHECK(worst < 100 && most <= 1 << 16 && key_at && key_at - t0 < 400);
	proc_kill(id);
	while (!done) CHECK(loop_once(100) == 0);
	loop_unwatch((int)(INT_PTR)k);
	CloseHandle(k);
}

/* The exit seen while the child's output is still in the pipe (a watch
   function sleeps over its whole life): report reads it first. */
static void on_slow(void *ctx, int fd) { (void)ctx; (void)fd; Sleep(1500); }

static void drain(void)
{
	HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
	CHECK(e && loop_watch((int)(INT_PTR)e, on_slow, NULL) == 0);
	CHECK(spawn(&g, "say", NULL) >= 0);
	SetEvent(e);
	CHECK(loop_once(-1) == 0);               /* the child writes and exits meanwhile */
	CHECK(g.ended[0] && g.order && g.code == 7 && g.nout == 6 && !memcmp(g.out, "hello\n", 6));
	free(g.out);
	loop_unwatch((int)(INT_PTR)e);
	CloseHandle(e);
}

/* A proc_write from the callback inside the exit's drain, to a child whose
   stdin a grandchild still holds, is refused: begun, it would complete on
   the slot's next child and close its stdin (review of PR 41, 2). */
static int asker = -1, answer = -2;
static DWORD asker_gc;
static void on_ask(void *ctx, int s, const char *b, size_t n)
{
	static char mb[1 << 20];
	if (s == 1 && n && answer == -2) { asker_gc = (DWORD)atol(b); answer = proc_write(asker, mb, sizeof mb); }
	if (!s) *(int *)ctx = 1;
}

static void stale(void)
{
	HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
	int done = 0;
	char *argv[] = { self, "holdin", NULL };
	CHECK(e && loop_watch((int)(INT_PTR)e, on_slow, NULL) == 0);
	CHECK((asker = proc_spawn(argv, NULL, on_ask, &done)) >= 0);
	SetEvent(e);
	CHECK(loop_once(-1) == 0);               /* its question is still in the pipe at the exit */
	loop_unwatch((int)(INT_PTR)e);
	CloseHandle(e);
	CHECK(done && answer == -1);
	int id = spawn(&g, "cat", NULL);
	CHECK(id == asker);                      /* the same slot */
	CHECK(proc_write(id, "abc", 3) == 0);
	for (int i = 0; i < 5; i++) CHECK(loop_once(100) == 0);
	CHECK(proc_write(id, "def", 3) == 0);
	proc_close(id);
	run(&g, 5000);
	CHECK(g.code == 0 && g.nout == 6 && !memcmp(g.out, "abcdef", 6));
	free(g.out);
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, asker_gc);
	if (h) { TerminateProcess(h, 0); CloseHandle(h); }
}

/* told of its exit, a child starts another, which gets its slot */
static Got next;
static int next_id = -1;
static void respawn(void *ctx, int s, const char *b, size_t n)
{
	on_proc(ctx, s, b, n);
	if (!s) next_id = spawn(&next, "sleep", NULL);
}

/* proc_wait (Wait.tla "ok"): only its child runs, even with another
   child's reads armed and its output there; the others come after it */
static void waits(void)
{
	static Got a;
	char name[32];   /* this run's own: two runs may share a session */
	snprintf(name, sizeof name, "homeros-say-%lu", GetCurrentProcessId());
	SetEnvironmentVariableA("HOMEROS_SAY", name);
	HANDLE m = CreateMutexA(NULL, TRUE, name);   /* a says nothing before its reads are armed */
	CHECK(m && spawn(&a, "say", NULL) >= 0);
	int id = spawn(&g, "slowcat", NULL);
	CHECK(id >= 0 && loop_once(0) == 0);     /* a's reads are armed */
	CHECK(loop_timer(tick, &fired[4], 0) == 0);
	/* a has written and exited: its mutex is abandoned (a Sleep was
	   too short under a loaded Wine, PR 114's review) */
	do { ReleaseMutex(m); Sleep(1); } while (WaitForSingleObject(m, INFINITE) != WAIT_ABANDONED);
	CloseHandle(m);
	CHECK(proc_write(id, "hello", 5) == 0);
	proc_close(id);
	CHECK(proc_wait(id, 3000) == 0);
	CHECK(g.ended[0] && g.order && g.code == 0 && g.nout == 5 && !memcmp(g.out, "hello", 5));
	CHECK(!a.nout && !a.ended[1] && !a.ended[0] && !fired[4]);
	run(&a, 2000);
	CHECK(a.nout == 6 && a.code == 7 && fired[4] == 1);
	free(a.out); free(g.out);
	id = spawn(&g, "sleep", NULL);
	long long t0 = ms();
	CHECK(proc_wait(id, 200) == -1 && g.ended[0] && g.code == 128 + 15);
	CHECK(ms() - t0 >= 190 && ms() - t0 < 700);
	free(g.out);
	/* the wait ends at its child's report, not at the slot's next child */
	char *argv[] = { self, "say", NULL };
	memset(&g, 0, sizeof g);
	id = proc_spawn(argv, NULL, respawn, &g);
	t0 = ms();
	CHECK(proc_wait(id, 1000) == 0 && g.ended[0] && next_id == id && ms() - t0 < 500);
	free(g.out);
	proc_kill(next_id);
	run(&next, 2000);
	free(next.out);
}

static void idle(void)
{
	HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
	CHECK(loop_watch((int)(INT_PTR)e, on_ev, (void *)0) == 0);
	int id = spawn(&g, "sleep", NULL);
	CHECK(id >= 0);
	CHECK(loop_timer(tick, &fired[0], 3600 * 1000) == 0);
	int r0 = ran[0];
	long long t0 = ms();
	CHECK(loop_once(2000) == 0);
	long long took = ms() - t0;
	fprintf(stderr, "test_loop_win (measured): idle loop_once(2000) returned after %lld ms\n", took);
	CHECK(took >= 1990 && ran[0] == r0 && !g.ended[0]);
	proc_kill(id);
	run(&g, 2000);
	loop_timer(tick, &fired[0], -1);
	loop_unwatch((int)(INT_PTR)e);
	CloseHandle(e);
}

static SOCKET sa;
static long woke;
static void on_sock(void *ctx, int fd)
{
	WSANETWORKEVENTS ne;
	(void)ctx;
	CHECK(WSAEnumNetworkEvents(sa, (HANDLE)(INT_PTR)fd, &ne) == 0);   /* resets the event */
	woke |= ne.lNetworkEvents;
}

static void sockets(void)
{
	static char buf[1 << 16];
	WSADATA wd;
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	int n = sizeof a;
	CHECK(WSAStartup(MAKEWORD(2, 2), &wd) == 0);
	SOCKET l = socket(AF_INET, SOCK_STREAM, 0), b;
	CHECK(!bind(l, (struct sockaddr *)&a, sizeof a) && !listen(l, 1) && !getsockname(l, (struct sockaddr *)&a, &n));
	sa = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(!connect(sa, (struct sockaddr *)&a, sizeof a) && (b = accept(l, NULL, NULL)) != INVALID_SOCKET);
	WSAEVENT ev = WSACreateEvent();
	int fd = (int)(INT_PTR)ev;
	CHECK(!WSAEventSelect(sa, ev, FD_READ | FD_WRITE | FD_CLOSE));   /* also makes sa non-blocking */
	CHECK(loop_watch(fd, on_sock, NULL) == 0);
	loop_write(fd, 1);
	loop_once(0);                      /* the FD_WRITE Winsock posts at once */
	while (send(sa, buf, sizeof buf, 0) > 0) {}
	CHECK(WSAGetLastError() == WSAEWOULDBLOCK);
	woke = 0;
	CHECK(loop_once(0) == 0 && !(woke & FD_WRITE));   /* full: no wake */
	u_long nb = 1;
	CHECK(!ioctlsocket(b, FIONBIO, &nb));
	for (int i = 0; i < 50 && !(woke & FD_WRITE); i++) {
		while (recv(b, buf, sizeof buf, 0) > 0) {}
		loop_once(100);
	}
	CHECK(woke & FD_WRITE);            /* room again: the watch ran */
	loop_unwatch(fd);
	closesocket(sa); closesocket(b); closesocket(l);
	WSACloseEvent(ev);
	WSACleanup();
}

int main(int argc, char **argv)
{
	GetModuleFileNameA(NULL, self, sizeof self);
	if (argc > 1) return child(argv);
	pipes();
	kills();
	holder();
	timers();
	watches();
	ctrl_c();
	idle();
	flood();
	drain();
	stale();
	sockets();
	pty();
	waits();
	fprintf(stderr, "test_loop_win: pseudoconsole; flood, drain and a write refused while reaping; 4 MB through cat; dead.py; streams, codes, arguments, directory, closed stdin; 32 children; kill takes the tree; exit with a holder, its pipes closed; timers; every ready watch, a taken signal kept, unwatch in a pass; message queue; Ctrl-C; idle; proc_wait; socket write wake\n");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_loop_win: Windows only (src/loop/check.sh runs it under Wine)"); return 0; }
#endif
