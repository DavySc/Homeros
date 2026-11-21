/* loop.h: child processes on non-blocking pipes (a child that writes
   before it reads cannot deadlock the editor: design/reviews/r1/dead.py),
   stdout and stderr apart, exit codes after all output, the kill escalation
   over the whole process group, timers, Ctrl-C inside an endless command
   (Neovim stops `:while 1`), and 0 wake-ups while idle with a child, a
   watch and a timer set.  Also, from the package 10 review: the exit is
   reported when the child exits, not when what it left behind closes the
   pipes (Neovim's on_exit); SIGKILL comes for every killed child; a failed
   write stops the polling; the queue stays as large as its backlog; a
   watch removed by another watch's function does not run.  proc_wait
   (Neovim's system(), Wait.tla "ok"): only its child runs, another child's
   exit during the wait is still reported after it, ms or Ctrl-C kill. */
#include <dirent.h>
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

	char *cat[] = { "cat", NULL };
	memset(&g, 0, sizeof g);
	int c = proc_spawn(cat, NULL, on_proc, &g);
	CHECK(c >= 0 && proc_write(c, NULL, 0) == 0);   /* nothing queued yet: no memcpy to NULL */
	proc_close(c);
	run(&g, 5000);
	CHECK(g.code == 0 && !g.nout);

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
/* A process that is gone or a zombie. */
static int dead(int pid)
{
	char path[64], st[256];
	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	FILE *f = fopen(path, "r");
	if (!f) return 1;
	int z = fgets(st, sizeof st, f) && strstr(st, ") Z ");
	fclose(f);
	return z;
}

static void kills(void)
{
	Got g;
	/* the grandchild dies with the child (the group kill) */
	int id = spawn(&g, "sleep 100 & echo $!; wait");
	CHECK(id >= 0);
	while (!g.nout || g.out[g.nout - 1] != '\n') CHECK(loop_once(1000) == 0);
	int grandchild = atoi(g.out);
	proc_kill(id);
	run(&g, 1000);
	CHECK(g.code == 128 + SIGTERM && grandchild > 0);
	usleep(100000);
	CHECK(dead(grandchild));
	free(g.out);

	/* SIGKILL 2 s later when SIGTERM is ignored; loop_once(-1) must wake
	   for it, as for a timer */
	id = spawn(&g, "trap '' TERM; sleep 100 & echo $!; wait");
	CHECK(id >= 0);
	while (!g.nout || g.out[g.nout - 1] != '\n') CHECK(loop_once(1000) == 0);
	grandchild = atoi(g.out);
	long long t0 = ms();
	proc_kill(id);
	while (!g.ended[0]) CHECK(loop_once(-1) == 0);
	long long took = ms() - t0;
	CHECK(g.code == 128 + SIGKILL && took >= 1990 && took < 2500);
	usleep(100000);
	CHECK(dead(grandchild));                 /* the SIGKILL went to the group too */
	free(g.out);

	/* a child that dies on the SIGTERM leaves no SIGKILL behind: its free
	   slot has pid 0, and kill(-0) would hit the editor's own group.  In a
	   forked editor with its own session, so the test survives it. */
	pid_t ed = fork();
	if (!ed) {
		setsid();
		id = spawn(&g, "sleep 100");
		CHECK(id >= 0);
		proc_kill(id);
		run(&g, 1000);
		for (long long t1 = ms(); ms() - t1 < 2500;) loop_once(100);
		_exit(0);
	}
	int st;
	CHECK(waitpid(ed, &st, 0) == ed && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	CHECK(loop_once(0) == 0);                /* the SIGCHLD wake-ups it left in the shared pipe */
}

/* What a child wrote before it exited all arrives before its exit, even
   when it enlarged its pipe past one read (F_SETPIPE_SZ). */
static void big_pipe(void)
{
	Got g;
	char *argv[] = { "python3", "-c", "import fcntl,os; fcntl.fcntl(1, 1031, 1 << 20); os.write(1, b'x' * 500000)", NULL };
	memset(&g, 0, sizeof g);
	CHECK(proc_spawn(argv, NULL, on_proc, &g) >= 0);
	usleep(500000);                          /* it has written and exited */
	run(&g, 2000);
	CHECK(g.code == 0 && g.nout == 500000);
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
	CHECK(!loop_waiting());
	CHECK(spawn(&g, "echo hi") >= 0);
	CHECK(loop_timer(tick, &fired[3], 0) == 0);
	pid_t w = fork();
	if (!w) { usleep(300000); if (write(input[1], "jk\3", 3)) {} _exit(0); }
	long long t0 = ms(), calls = 0;
	volatile unsigned long long work = 0;
	while (!loop_breakcheck() && ms() - t0 < 2000) { for (int i = 0; i < 1000; i++) work += (unsigned)i; calls++; }
	long long took = ms() - t0;
	waitpid(w, NULL, 0);
	CHECK(!loop_waiting());                        /* the read function took it all */
	CHECK(write(input[1], "x", 1) == 1 && loop_waiting() && loop_waiting());   /* and reads nothing */
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

/* The exit comes with the child's output, though a process it started
   still holds the pipes (Neovim: 1 to 2 ms; review finding 2). */
static int open_fds(void)
{
	int n = 0;
	DIR *d = opendir("/proc/self/fd");
	CHECK(d);
	while (readdir(d)) n++;
	closedir(d);
	return n;
}

static void holder(void)
{
	Got g;
	int fds = open_fds();
	long long t0 = ms();
	CHECK(spawn(&g, "sleep 3 & echo hi") >= 0);
	run(&g, 1000);
	CHECK(g.code == 0 && g.nout == 3 && !memcmp(g.out, "hi\n", 3) && ms() - t0 < 500);
	CHECK(open_fds() == fds);                /* its pipes are closed at the exit */
	free(g.out);
	/* nothing to write to or kill once reported (finding 3) */
	char *argv[] = { "true", NULL };
	int id = proc_spawn(argv, NULL, on_proc, &g);
	memset(&g, 0, sizeof g);
	CHECK(id >= 0);
	run(&g, 1000);
	CHECK(proc_write(id, "x", 1) == -1);
}

/* Every killed child that ignores SIGTERM gets its SIGKILL, however many
   (review finding 4: 20 kills through a 16-slot timer table left 4). */
static void kill_many(void)
{
	enum { N = 32 };
	Got g[N];
	int id[N];
	for (int i = 0; i < N; i++) CHECK((id[i] = spawn(&g[i], "trap '' TERM; while :; do sleep 1; done")) >= 0);
	usleep(200000);
	for (int i = 0; i < N; i++) proc_kill(id[i]);
	for (int i = 0; i < N; i++) { run(&g[i], 3000); CHECK(g[i].code == 128 + SIGKILL); free(g[i].out); }
}

/* A child that closes its stdin with bytes still queued: the write fails
   once and stdin is dropped, rather than poll reporting the broken pipe
   on every pass until the child exits. */
static void broken_stdin(void)
{
	static char mb[1 << 20];
	Got g;
	int id = spawn(&g, "sleep 0.2; exec 0<&-; sleep 1");
	CHECK(id >= 0);
	CHECK(proc_write(id, mb, sizeof mb) == 0);
	int passes = 0;
	while (!g.ended[0]) { CHECK(loop_once(2000) == 0); passes++; }
	CHECK(passes < 10 && g.code == 0);
}

/* The queue keeps only its backlog: 256 MB through cat with 1 to 2 MB in
   flight must not grow the peak RSS by the total (the queue's compaction). */
static long peak_kb(void)
{
	char line[256];
	long kb = 0;
	FILE *f = fopen("/proc/self/status", "r");
	CHECK(f);
	while (fgets(line, sizeof line, f)) if (!strncmp(line, "VmHWM:", 6)) kb = atol(line + 6);
	fclose(f);
	return kb;
}

static size_t got_back;
static void count_out(void *ctx, int s, const char *b, size_t n) { (void)b; if (s == 1) got_back += n; if (!s) *(int *)ctx = 1; }

static void backlog(void)
{
	static char chunk[256 << 10];
	const size_t total = (size_t)256 << 20;
	char *argv[] = { "cat", NULL };
	int done = 0;
	size_t sent = 0;
	long before = peak_kb();
	int id = proc_spawn(argv, NULL, count_out, &done);
	CHECK(id >= 0);
	while (got_back < total) {
		if (sent < total && sent - got_back < (2u << 20)) { CHECK(proc_write(id, chunk, sizeof chunk) == 0); sent += sizeof chunk; }
		CHECK(loop_once(100) == 0);
	}
	proc_close(id);
	while (!done) CHECK(loop_once(100) == 0);
	long grew = peak_kb() - before;
	fprintf(stderr, "test_loop: peak RSS grew %ld KB while 256 MB passed through cat\n", grew);
	CHECK(grew < 64 * 1024);
}

/* A watch function that removes another watch stops it from running, in
   loop_once and in loop_breakcheck alike. */
static int wa[2], wb[2], ran_b;
static void on_a(void *ctx, int fd) { char c; (void)ctx; CHECK(read(fd, &c, 1) == 1); loop_unwatch(wb[0]); }
static void on_b(void *ctx, int fd) { (void)ctx; (void)fd; ran_b = 1; }

static void unwatch_in_callback(void)
{
	for (int round = 0; round < 2; round++) {
		CHECK(pipe(wa) == 0 && pipe(wb) == 0);
		CHECK(loop_watch(wa[0], on_a, NULL) == 0 && loop_watch(wb[0], on_b, NULL) == 0);
		CHECK(write(wa[1], "a", 1) == 1 && write(wb[1], "b", 1) == 1);
		ran_b = 0;
		if (round == 0) CHECK(loop_once(0) == 0);
		else for (int i = 0; i < 32; i++) loop_breakcheck();
		CHECK(!ran_b);
		loop_unwatch(wa[0]);
		close(wa[0]); close(wa[1]); close(wb[0]); close(wb[1]);
	}
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

static void waits(void)
{
	Got a, b;
	int ka;
	CHECK(pipe(input) == 0);
	CHECK(loop_watch(input[0], on_input, NULL) == 0);
	/* a exits at once and says something; b is a filter */
	CHECK(spawn(&a, "echo a") >= 0);
	CHECK(loop_timer(tick, &fired[4], 0) == 0);
	int id = spawn(&b, "sleep 0.2; cat");
	CHECK(id >= 0 && proc_write(id, "hello", 5) == 0);
	proc_close(id);
	long long t0 = ms();
	clock_t c0 = clock();
	CHECK(proc_wait(id, 2000) == 0);
	CHECK(clock() - c0 < CLOCKS_PER_SEC / 20);   /* the due timer does not spin it */
	CHECK(b.ended[0] && b.order && b.code == 0 && b.nout == 5 && !memcmp(b.out, "hello", 5));
	CHECK(!a.nout && !a.ended[0] && !fired[4]);   /* deferred */
	run(&a, 2000);                                  /* its exit was not lost */
	CHECK(a.nout == 2 && fired[4] == 1);
	free(a.out); free(b.out);
	/* ms passing kills it */
	id = spawn(&b, "sleep 10");
	t0 = ms();
	CHECK(proc_wait(id, 200) == -1 && b.ended[0] && b.code == 128 + SIGTERM);
	CHECK(ms() - t0 >= 200 && ms() - t0 < 600);
	/* an ignored SIGTERM: SIGKILL 2 s later, still reported before it returns */
	id = spawn(&b, "trap '' TERM; echo; sleep 10");
	while (!b.nout) CHECK(loop_once(100) == 0);   /* the trap is set */
	t0 = ms();
	CHECK(proc_wait(id, 100) == -1 && b.ended[0] && b.code == 128 + SIGKILL);
	CHECK(ms() - t0 >= 2000 && ms() - t0 < 2600);
	free(b.out);
	/* Ctrl-C kills it */
	id = spawn(&b, "sleep 10");
	pid_t w = fork();
	if (!w) { usleep(100000); if (write(input[1], "\3", 1)) {} _exit(0); }
	t0 = ms();
	ka = proc_wait(id, 5000);
	CHECK(ka == -1 && loop_int && b.ended[0] && ms() - t0 < 600);
	waitpid(w, NULL, 0);
	fprintf(stderr, "test_loop: proc_wait: others deferred and reported after; killed at 200 ms, SIGKILL after 2 s, Ctrl-C after %lld ms\n", ms() - t0);
	loop_int = 0;
	loop_unwatch(input[0]);
	close(input[0]); close(input[1]);
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
	/* GitHub's runner starts steps with SIGPIPE ignored: the check must not
	   depend on who started it (review finding 1) */
	signal(SIGPIPE, SIG_DFL);
	signal(SIGALRM, watchdog);
	alarm(60);     /* a blocking write or a lost exit hangs; that is a failure */
	echo();
	no_deadlock();
	streams_and_codes();
	kills();
	timers();
	ctrl_c();
	no_leak();
	holder();
	big_pipe();
	kill_many();
	broken_stdin();
	backlog();
	unwatch_in_callback();
	many();
	waits();
	alarm(0);
	idle();
	return 0;
}
