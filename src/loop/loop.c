/* The event loop: poll(2) over the watched fds, the child processes' pipes
   and a self-pipe for SIGCHLD, with timers as poll's timeout.  Writes to a
   child never block: what the pipe does not take waits in that child's
   queue until poll says the pipe has room (Pipe.tla "queue"; a blocking
   write deadlocks against a child that writes before it reads, as
   design/reviews/r1/dead.py shows). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "loop.h"

enum { MAXW = 16, MAXT = 16, MAXP = 32, MAXF = MAXW + 1 + 3 * MAXP, KILL_MS = 2000 };

int loop_int;

static struct { int fd; LoopFn fn; void *ctx; } w[MAXW];   /* fd 0 marks a free slot as fd + 1 */
static struct { void (*fn)(void *); void *ctx; long long at; } t[MAXT];
static struct Proc {
	pid_t pid;              /* 0: free slot */
	int fd[3];              /* stdin, stdout, stderr; -1 once closed */
	int code;               /* exit code once reaped, else -1 */
	int closing;            /* close stdin when the queue is empty */
	ProcFn fn;
	void *ctx;
	char *q;                /* bytes for stdin not yet taken by the pipe */
	size_t qoff, qlen, qcap;
} p[MAXP];
static int chld[2] = { -1, -1 };
static char buf[1 << 16];

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int loop_watch(int fd, LoopFn fn, void *ctx)
{
	for (int i = 0; i < MAXW; i++)
		if (!w[i].fd) { w[i].fd = fd + 1; w[i].fn = fn; w[i].ctx = ctx; return 0; }
	return -1;
}

void loop_unwatch(int fd)
{
	for (int i = 0; i < MAXW; i++) if (w[i].fd == fd + 1) w[i].fd = 0;
}

int loop_timer(void (*fn)(void *), void *ctx, int ms)
{
	int f = -1;
	for (int i = 0; i < MAXT; i++) {
		if (t[i].fn == fn && t[i].ctx == ctx) { t[i].fn = NULL; f = i; break; }
		if (!t[i].fn && f < 0) f = i;
	}
	if (ms < 0) return 0;
	if (f < 0) return -1;
	t[f].fn = fn; t[f].ctx = ctx; t[f].at = now() + ms * 1000000LL;
	return 0;
}

/* Neovim's line_breakcheck counts to 32 before it looks at input, so a
   caller in a tight loop pays one poll per 32 calls. */
int loop_breakcheck(void)
{
	static int count;
	struct pollfd pf[MAXW];
	int slot[MAXW], n = 0;
	if (loop_int || ++count < 32) return loop_int;
	count = 0;
	for (int i = 0; i < MAXW; i++)
		if (w[i].fd) { pf[n] = (struct pollfd){ .fd = w[i].fd - 1, .events = POLLIN }; slot[n++] = i; }
	if (poll(pf, (nfds_t)n, 0) > 0)
		for (int k = 0; k < n; k++)
			if (pf[k].revents && w[slot[k]].fd == pf[k].fd + 1) w[slot[k]].fn(w[slot[k]].ctx, pf[k].fd);
	return loop_int;
}

static void on_chld(int sig)
{
	int e = errno;
	(void)sig;
	if (write(chld[1], "c", 1) < 0) {}   /* a full pipe already holds a wake-up */
	errno = e;
}

static int setup(void)
{
	if (chld[0] >= 0) return 0;
	if (pipe(chld)) return -1;
	for (int i = 0; i < 2; i++) {
		fcntl(chld[i], F_SETFL, O_NONBLOCK);
		fcntl(chld[i], F_SETFD, FD_CLOEXEC);
	}
	struct sigaction sa = { .sa_handler = on_chld, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
	sigaction(SIGCHLD, &sa, NULL);
	/* a child that closed its stdin gives EPIPE on write, not a dead editor */
	signal(SIGPIPE, SIG_IGN);
	return 0;
}

int proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx)
{
	extern char **environ;
	int id = 0, fd[6] = { -1, -1, -1, -1, -1, -1 }, ok = 0;
	while (id < MAXP && p[id].pid) id++;
	if (id == MAXP || setup()) return -1;
	if (pipe(fd) || pipe(fd + 2) || pipe(fd + 4)) goto out;
	for (int i = 0; i < 6; i++) fcntl(fd[i], F_SETFD, FD_CLOEXEC);
	/* the editor's ends: stdin's write end, stdout's and stderr's read ends */
	fcntl(fd[1], F_SETFL, O_NONBLOCK);
	fcntl(fd[2], F_SETFL, O_NONBLOCK);
	fcntl(fd[4], F_SETFL, O_NONBLOCK);
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	sigset_t def, none;
	posix_spawn_file_actions_init(&fa);
	posix_spawnattr_init(&at);
	posix_spawn_file_actions_adddup2(&fa, fd[0], 0);
	posix_spawn_file_actions_adddup2(&fa, fd[3], 1);
	posix_spawn_file_actions_adddup2(&fa, fd[5], 2);
	if (dir) posix_spawn_file_actions_addchdir_np(&fa, dir);
	/* the editor ignores SIGPIPE; git and friends expect the default */
	sigemptyset(&def); sigaddset(&def, SIGPIPE); sigaddset(&def, SIGCHLD);
	sigemptyset(&none);
	posix_spawnattr_setsigdefault(&at, &def);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setpgroup(&at, 0);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);
	pid_t pid;
	ok = !posix_spawnp(&pid, argv[0], &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	if (ok) p[id] = (struct Proc){ .pid = pid, .fd = { fd[1], fd[2], fd[4] }, .code = -1, .fn = fn, .ctx = ctx };
out:
	for (int i = 0; i < 6; i++)
		if (fd[i] >= 0 && (!ok || !(i == 1 || i == 2 || i == 4))) close(fd[i]);
	return ok ? id : -1;
}

static void close_in(struct Proc *c)
{
	close(c->fd[0]);
	c->fd[0] = -1;
	free(c->q);
	c->q = NULL;
	c->qoff = c->qlen = c->qcap = 0;
}

/* Writes what the pipe takes now; 0 ok (some may stay queued), -1 the
   child's stdin is gone. */
static int flush(struct Proc *c)
{
	while (c->qoff < c->qlen) {
		ssize_t r = write(c->fd[0], c->q + c->qoff, c->qlen - c->qoff);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0 && errno == EAGAIN) return 0;
		if (r < 0) { close_in(c); return -1; }
		c->qoff += (size_t)r;
	}
	c->qoff = c->qlen = 0;
	if (c->closing) close_in(c);
	return 0;
}

static struct Proc *proc(int id)
{
	return id >= 0 && id < MAXP && p[id].pid ? &p[id] : NULL;
}

int proc_write(int id, const void *b, size_t n)
{
	struct Proc *c = proc(id);
	if (!c || c->fd[0] < 0 || c->closing) return -1;
	if (c->qlen + n > c->qcap) {
		if (c->qoff) { memmove(c->q, c->q + c->qoff, c->qlen - c->qoff); c->qlen -= c->qoff; c->qoff = 0; }
		size_t cap = c->qcap ? c->qcap : 4096;
		while (cap < c->qlen + n) cap *= 2;
		if (cap != c->qcap) {
			char *q = realloc(c->q, cap);
			if (!q) return -1;
			c->q = q;
			c->qcap = cap;
		}
	}
	memcpy(c->q + c->qlen, b, n);
	c->qlen += n;
	return flush(c);
}

void proc_close(int id)
{
	struct Proc *c = proc(id);
	if (!c || c->fd[0] < 0) return;
	c->closing = 1;
	if (c->qoff == c->qlen) close_in(c);
}

/* Signals the child's process group, so what it started dies with it and
   cannot hold its pipes open (Neovim's os_proc_tree_kill sends to -pid).
   The group id stays taken while the group has members, so -pid cannot
   reach a stranger before the slot is free. */
static void hard_kill(void *ctx)
{
	struct Proc *c = ctx;
	if (c->pid) kill(-c->pid, SIGKILL);
}

void proc_kill(int id)
{
	struct Proc *c = proc(id);
	if (!c) return;
	kill(-c->pid, SIGTERM);
	loop_timer(hard_kill, c, KILL_MS);
}

/* After a SIGCHLD reaps exited children; then reports those whose output
   has also ended. */
static void reap(int sig)
{
	char drain[64];
	if (sig) while (read(chld[0], drain, sizeof drain) > 0) {}
	for (int i = 0; i < MAXP; i++) {
		struct Proc *c = &p[i];
		int st;
		if (!c->pid) continue;
		if (sig && c->code < 0 && waitpid(c->pid, &st, WNOHANG) == c->pid)
			c->code = WIFSIGNALED(st) ? 128 + WTERMSIG(st) : WEXITSTATUS(st);
		if (c->code >= 0 && c->fd[1] < 0 && c->fd[2] < 0) {
			if (c->fd[0] >= 0) close_in(c);
			loop_timer(hard_kill, c, -1);
			ProcFn fn = c->fn;
			void *ctx = c->ctx;
			int code = c->code;
			c->pid = 0;
			fn(ctx, 0, NULL, (size_t)code);
		}
	}
}

int loop_once(int timeout_ms)
{
	static struct pollfd pf[MAXF];
	static short who[MAXF], what[MAXF];   /* slot, and -1 for a watch or the stream */
	int n = 0;
	for (int i = 0; i < MAXW; i++)
		if (w[i].fd) { pf[n] = (struct pollfd){ .fd = w[i].fd - 1, .events = POLLIN }; who[n] = (short)i; what[n++] = -1; }
	if (chld[0] >= 0) { pf[n] = (struct pollfd){ .fd = chld[0], .events = POLLIN }; who[n] = -1; what[n++] = -2; }
	for (int i = 0; i < MAXP; i++)
		for (int s = 0; s < 3 && p[i].pid; s++)
			if (p[i].fd[s] >= 0 && (s || p[i].qoff < p[i].qlen)) {
				pf[n] = (struct pollfd){ .fd = p[i].fd[s], .events = s ? POLLIN : POLLOUT };
				who[n] = (short)i; what[n++] = (short)s;
			}
	long long first = -1;
	for (int i = 0; i < MAXT; i++) if (t[i].fn && (first < 0 || t[i].at < first)) first = t[i].at;
	if (first >= 0) {
		long long d = first - now();
		int ms = d <= 0 ? 0 : (int)((d + 999999) / 1000000);   /* round up: never wake before it is due */
		if (timeout_ms < 0 || ms < timeout_ms) timeout_ms = ms;
	}
	int r = poll(pf, (nfds_t)n, timeout_ms), sig = 0;
	if (r < 0 && errno != EINTR) return -1;
	for (int k = 0; k < n && r > 0; k++) {
		if (!pf[k].revents) continue;
		r--;
		int i = who[k], s = what[k];
		if (s == -1) {
			if (w[i].fd == pf[k].fd + 1) w[i].fn(w[i].ctx, pf[k].fd);
			continue;
		}
		if (s < 0) { sig = 1; continue; }          /* SIGCHLD: reaped below */
		struct Proc *c = &p[i];
		if (!c->pid || c->fd[s] != pf[k].fd) continue;   /* closed by a callback meanwhile */
		if (!s) { flush(c); continue; }
		ssize_t got = read(c->fd[s], buf, sizeof buf);
		if (got < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (got > 0) { c->fn(c->ctx, s, buf, (size_t)got); continue; }
		close(c->fd[s]);
		c->fd[s] = -1;
		c->fn(c->ctx, s, NULL, 0);
	}
	long long tn = now();
	for (int i = 0; i < MAXT; i++)
		if (t[i].fn && t[i].at <= tn) {
			void (*fn)(void *) = t[i].fn;
			t[i].fn = NULL;
			fn(t[i].ctx);
		}
	if (chld[0] >= 0) reap(sig);
	return 0;
}
