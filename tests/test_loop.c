/* loop.h: child processes on non-blocking pipes (a child that writes
   before it reads cannot deadlock the editor: design/reviews/r1/dead.py),
   stdout and stderr apart, exit codes after all output, the kill escalation
   over the whole process group, timers, Ctrl-C inside an endless command
   (Neovim stops `:while 1`), and 0 wake-ups while idle with a child, a
   watch and a timer set. */
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "loop.h"

static long long ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* What one child said, in order of arrival. */
typedef struct {
	char *out; size_t nout, cap;
	char err[256]; size_t nerr;
	int ended[3], code, order;   /* order: 1 if both streams ended before the exit */
} Got;

static void on_proc(void *ctx, int s, const char *b, size_t n)
{
	Got *g = ctx;
	CHECK(!g->ended[0]);                       /* nothing after the exit */
	if (s == 0) { g->ended[0] = 1; g->code = (int)n; g->order = g->ended[1] && g->ended[2]; return; }
	if (!n) { CHECK(!g->ended[s]); g->ended[s] = 1; return; }
	CHECK(!g->ended[s] && b);
	if (s == 2) { CHECK(g->nerr + n < sizeof g->err); memcpy(g->err + g->nerr, b, n); g->nerr += n; return; }
	if (g->nout + n > g->cap) {
		g->cap = (g->nout + n) * 2;
		g->out = realloc(g->out, g->cap);
		CHECK(g->out);
	}
	memcpy(g->out + g->nout, b, n);
	g->nout += n;
}

static void run(Got *g, long long limit_ms)
{
	long long end = ms() + limit_ms;
	while (!g->ended[0]) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	CHECK(g->order);
}

static int spawn(Got *g, const char *sh)
{
	char *argv[] = { "sh", "-c", (char *)sh, NULL };
	memset(g, 0, sizeof *g);
	return proc_spawn(argv, NULL, on_proc, g);
}

/* Large input to cat comes back byte for byte, in pieces of any size. */
static void echo(void)
{
	enum { N = 4 << 20 };
	static char in[N];
	Got g;
	for (int i = 0; i < N; i++) in[i] = (char)rnd(256);
	char *argv[] = { "cat", NULL };
	memset(&g, 0, sizeof g);
	int id = proc_spawn(argv, NULL, on_proc, &g);
	CHECK(id >= 0);
	size_t off = 0;
	while (off < N) {                       /* random write sizes, interleaved with the loop */
		size_t k = 1 + rnd(200000);
		if (k > N - off) k = N - off;
		CHECK(proc_write(id, in + off, k) == 0);
		off += k;
		if (rnd(2)) CHECK(loop_once(0) == 0);
	}
	proc_close(id);
	CHECK(proc_write(id, "x", 1) == -1);   /* nothing after the close */
	run(&g, 10000);
	CHECK(g.code == 0 && g.nout == N && !memcmp(g.out, in, N) && !g.nerr);
	free(g.out);
}

/* dead.py: the child writes 200 KB before it reads; a blocking 1 MB write
   would wait forever, here it returns at once and both sides finish. */
static void no_deadlock(void)
{
	static char mb[1 << 20];
	Got g;
	int id = spawn(&g, "head -c 200000 /dev/zero; cat >/dev/null; echo done >&2");
	CHECK(id >= 0);
	long long t0 = ms();
	CHECK(proc_write(id, mb, sizeof mb) == 0);
	CHECK(ms() - t0 < 100);
	proc_close(id);
	run(&g, 10000);
	CHECK(g.code == 0 && g.nout == 200000 && g.nerr == 5 && !memcmp(g.err, "done\n", 5));
	free(g.out);
}

static void streams_and_codes(void)
{
	Got g;
	CHECK(spawn(&g, "echo out; echo err >&2; exit 3") >= 0);
	run(&g, 5000);
	CHECK(g.code == 3 && g.nout == 4 && !memcmp(g.out, "out\n", 4) && g.nerr == 4 && !memcmp(g.err, "err\n", 4));
	free(g.out);

	char *bad[] = { "/nonexistent/homeros-no-such-program", NULL };
	CHECK(proc_spawn(bad, NULL, on_proc, &g) == -1);

	char *pwd[] = { "pwd", NULL };
	memset(&g, 0, sizeof g);
	CHECK(proc_spawn(pwd, "/", on_proc, &g) >= 0);
	run(&g, 5000);
	CHECK(g.code == 0 && g.nout == 2 && !memcmp(g.out, "/\n", 2));
	free(g.out);

	/* the child reads one byte and exits: the rest of the write meets a
	   closed pipe, which must not kill the editor (SIGPIPE) */
	static char mb[1 << 20];
	int id = spawn(&g, "head -c 1 >/dev/null");
	CHECK(id >= 0);
	CHECK(proc_write(id, mb, sizeof mb) == 0);
	run(&g, 5000);
	CHECK(g.code == 0);
	CHECK(proc_write(id, "x", 1) == -1);   /* the id is free after the exit */

	/* the child sees SIGPIPE at its default, as git expects */
	CHECK(spawn(&g, "yes | head -c 1 >/dev/null") >= 0);   /* ignored, yes would complain */
	run(&g, 5000);
	CHECK(g.code == 0 && !g.nerr);
	free(g.out);
}

/* proc_kill: SIGTERM to the group; a child that ignores it gets SIGKILL
   after 2 s; a grandchild holding the pipes dies too. */
static void kills(void)
{
	Got g;
	int id = spawn(&g, "sleep 100 & sleep 100");
	CHECK(id >= 0);
	CHECK(loop_once(50) == 0);
	proc_kill(id);
	run(&g, 1000);
	CHECK(g.code == 128 + SIGTERM);

	id = spawn(&g, "trap '' TERM; sleep 100 & sleep 100");
	CHECK(id >= 0);
	usleep(100000);                          /* let the shell set its trap */
	long long t0 = ms();
	proc_kill(id);
	run(&g, 4000);
	long long took = ms() - t0;
	CHECK(g.code == 128 + SIGKILL && took >= 1990 && took < 2500);
}

static int fired[4];
static void tick(void *ctx) { fired[(int *)ctx - fired]++; }

static void timers(void)
{
	long long t0 = ms();
	CHECK(loop_timer(tick, &fired[0], 300) == 0);
	CHECK(loop_timer(tick, &fired[1], 100) == 0);
	CHECK(loop_timer(tick, &fired[2], 50) == 0);
	CHECK(loop_timer(tick, &fired[1], 200) == 0);   /* replaces the 100 */
	CHECK(loop_timer(tick, &fired[2], -1) == 0);    /* cancels */
	CHECK(loop_once(-1) == 0);                      /* forever, but a timer is due */
	CHECK(fired[1] == 1 && !fired[0] && ms() - t0 >= 200);
	CHECK(loop_once(-1) == 0);
	CHECK(fired[0] == 1 && fired[1] == 1 && !fired[2] && ms() - t0 >= 300 && ms() - t0 < 400);
	static char slot[17];
	for (int i = 0; i < 16; i++) CHECK(loop_timer(tick, &slot[i], 1000) == 0);
	CHECK(loop_timer(tick, &slot[16], 1000) == -1);              /* full */
	CHECK(loop_timer(tick, &slot[3], 500) == 0);                 /* a replace still fits */
	for (int i = 0; i < 16; i++) CHECK(loop_timer(tick, &slot[i], -1) == 0);
}

/* Ctrl-C: an endless command that calls loop_breakcheck stops soon after
   the key arrives; processes and timers wait until the command is over. */
static int input[2];
static void on_input(void *ctx, int fd)
{
	char b[64];
	(void)ctx;
	ssize_t n = read(fd, b, sizeof b);
	for (ssize_t i = 0; i < n; i++) if (b[i] == 3) loop_int = 1;
}

static void ctrl_c(void)
{
	Got g;
	CHECK(pipe(input) == 0);
	CHECK(loop_watch(input[0], on_input, NULL) == 0);
	CHECK(spawn(&g, "echo hi") >= 0);
	CHECK(loop_timer(tick, &fired[3], 0) == 0);
	pid_t w = fork();
	if (!w) { usleep(300000); if (write(input[1], "jk\3", 3)) {} _exit(0); }
	long long t0 = ms(), calls = 0;
	volatile unsigned long long work = 0;
	while (!loop_breakcheck() && ms() - t0 < 2000) { for (int i = 0; i < 1000; i++) work += (unsigned)i; calls++; }
	long long took = ms() - t0;
	waitpid(w, NULL, 0);
	CHECK(took >= 290 && took < 400);
	CHECK(!fired[3] && !g.nout && !g.ended[1]);   /* nothing ran inside the command */
	fprintf(stderr, "test_loop: Ctrl-C stopped the command %lld ms after the start (key sent at 300 ms), %lld breakchecks\n", took, calls);
	loop_int = 0;
	run(&g, 5000);
	CHECK(fired[3] == 1 && g.nout == 3);
	free(g.out);
	loop_unwatch(input[0]);
	close(input[0]); close(input[1]);
}

static void unused_proc(void *ctx, int s, const char *b, size_t n) { (void)ctx; (void)s; (void)b; (void)n; }

/* The design's gate: 0 wake-ups in 20 s idle.  The idle editor has a
   child, a watched fd and a timer far ahead; its context switches are read
   from /proc before and after. */
static long switches(pid_t pid)
{
	char path[64], line[256];
	long n = 0;
	snprintf(path, sizeof path, "/proc/%d/status", (int)pid);
	FILE *f = fopen(path, "r");
	CHECK(f);
	while (fgets(line, sizeof line, f))
		if (strstr(line, "ctxt_switches:")) n += atol(strchr(line, ':') + 1);
	fclose(f);
	return n;
}

static void idle(void)
{
	int sec = getenv("IDLE_SECONDS") ? atoi(getenv("IDLE_SECONDS")) : 20;
	pid_t pid = fork();
	if (!pid) {
		char *argv[] = { "cat", NULL };
		CHECK(pipe(input) == 0);
		CHECK(loop_watch(input[0], on_input, NULL) == 0);
		CHECK(proc_spawn(argv, NULL, unused_proc, NULL) >= 0);
		CHECK(loop_timer(tick, &fired[0], 3600 * 1000) == 0);
		for (;;) loop_once(-1);
	}
	usleep(500000);
	long a = switches(pid);
	sleep((unsigned)sec);
	long b = switches(pid);
	kill(-pid, SIGKILL);
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	fprintf(stderr, "test_loop: %ld wake-ups in %d s idle\n", b - a, sec);
	CHECK(b - a == 0);
}

/* The editor's ends of a child's pipes must not leak into later children,
   or the first child's stdin stays open while the second one lives. */
static void no_leak(void)
{
	Got a, b;
	char *cat[] = { "cat", NULL };
	memset(&a, 0, sizeof a);
	int ia = proc_spawn(cat, NULL, on_proc, &a), ib = spawn(&b, "sleep 3");
	CHECK(ia >= 0 && ib >= 0);
	proc_close(ia);
	run(&a, 1000);
	proc_kill(ib);
	run(&b, 1000);
}

static void many(void)
{
	Got g[33];
	int id[33];
	static char kb[256 << 10];
	for (int i = 0; i < 32; i++) {
		char *argv[] = { "cat", NULL };
		memset(&g[i], 0, sizeof g[i]);
		id[i] = proc_spawn(argv, NULL, on_proc, &g[i]);
		CHECK(id[i] >= 0);
		CHECK(proc_write(id[i], kb, sizeof kb) == 0);
		proc_close(id[i]);
	}
	char *argv[] = { "true", NULL };
	CHECK(proc_spawn(argv, NULL, on_proc, &g[32]) == -1);   /* full */
	for (int i = 0; i < 32; i++) run(&g[i], 10000);
	for (int i = 0; i < 32; i++) { CHECK(g[i].code == 0 && g[i].nout == sizeof kb); free(g[i].out); }
}

static void watchdog(int sig)
{
	(void)sig;
	static const char m[] = "test_loop: CHECK(no hang) failed: a test ran over 60 s\n";
	if (write(2, m, sizeof m - 1)) {}
	_exit(1);
}

int main(void)
{
	signal(SIGALRM, watchdog);
	alarm(60);     /* a blocking write or a lost exit hangs; that is a failure */
	echo();
	no_deadlock();
	streams_and_codes();
	kills();
	timers();
	ctrl_c();
	no_leak();
	many();
	alarm(0);
	idle();
	return 0;
}
