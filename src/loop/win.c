/* The event loop on Windows: the twin of loop.c.  One
   MsgWaitForMultipleObjectsEx over the watched handles, the message queue
   (LOOP_MSGQ) and the children's process handles.  The children's pipes
   are overlapped named pipes (DESIGN.md "Pipes": anonymous pipes cannot be
   waited on), as libuv makes them for Neovim; their I/O completes as APCs,
   which run only in that wait (MWMO_ALERTABLE), so never inside
   loop_breakcheck or loop_waiting, as loop.h asks.  An APC needs no wait
   handle, so the 63 handles a wait takes cover 16 watches and 32 children.
   Reads are zero-byte reads, as libuv's on pipes: a pending read holds no
   buffer, and the bytes are then read from the one shared buffer. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"
#include "timer.h"

/* Neovim reports a job stopped by jobstop as 128 + SIGTERM on Windows too.
   MAXW: as many watches as MsgWaitForMultipleObjectsEx waits on beside
   every child (MAXIMUM_WAIT_OBJECTS - 1 handles); each open folder, SQL
   session and LSP pipe takes one */
enum { MAXP = 32, MAXW = MAXIMUM_WAIT_OBJECTS - 1 - MAXP, KILLED = 128 + 15 };

int loop_int;

/* pend: a wait took the handle's signal (an auto-reset event or timer)
   before its function ran; the next loop_once runs it */
static struct { int used, fd, pend; LoopFn fn; void *ctx; } w[MAXW];
static struct Proc {
	HANDLE ph, job;         /* ph NULL: free slot */
	HPCON pc;               /* proc_pty's pseudoconsole, else NULL */
	HANDLE h[3];            /* our ends: stdin, stdout, stderr; NULL once closed */
	OVERLAPPED ov[3];       /* hEvent, which ReadFileEx leaves to us, holds 3 * slot + stream */
	int busy[3];            /* an I/O is in flight on h[s] */
	int closing, reaping;
	ProcFn fn;
	void *ctx;
	/* stdin: proc_write appends to q while out, which must not move, is
	   being written; they swap when out is done */
	char *q, *out;
	size_t qlen, qcap, outoff, outlen, outcap;
} p[MAXP];
static char buf[1 << 16];
/* proc_wait's child: no other child and no timer runs, the watches do, as
   in loop_breakcheck */
static int only = -1;
static int waited;   /* only was reported: a child its fn starts may get its slot */

#define H(fd) ((HANDLE)(INT_PTR)(fd))   /* sign-extended, as LongToHandle */

long long loop_now(void)
{
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return c.QuadPart / f.QuadPart * 1000000000LL + c.QuadPart % f.QuadPart * 1000000000LL / f.QuadPart;
}

int loop_watch(int fd, LoopFn fn, void *ctx)
{
	for (int i = 0; i < MAXW; i++)
		if (!w[i].used) { w[i].used = 1; w[i].fd = fd; w[i].pend = 0; w[i].fn = fn; w[i].ctx = ctx; return 0; }
	return -1;
}

/* loop.h: the socket's own event signals FD_WRITE after a would-block */
void loop_write(int fd, int on) { (void)fd; (void)on; }

void loop_unwatch(int fd)
{
	for (int i = 0; i < MAXW; i++) if (w[i].used && w[i].fd == fd) w[i].used = 0;
}

/* Is watch i ready, without waiting and without APCs?  Takes the signal of
   an auto-reset handle, so a caller that does not run the function marks
   it pending. */
static int ready(int i)
{
	if (w[i].fd == LOOP_MSGQ) return MsgWaitForMultipleObjectsEx(0, NULL, 0, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0;
	return WaitForSingleObject(H(w[i].fd), 0) == WAIT_OBJECT_0;
}

int loop_waiting(void)
{
	int any = 0;
	for (int i = 0; i < MAXW; i++)
		if (w[i].used && (w[i].pend || (w[i].pend = ready(i)))) any = 1;
	return any;
}

/* Runs the function of every ready watch; one removed by an earlier
   function in the pass does not run. */
static void run_ready(void)
{
	for (int i = 0; i < MAXW; i++)
		if (w[i].used && (w[i].pend || ready(i))) {
			w[i].pend = 0;
			w[i].fn(w[i].ctx, w[i].fd);
		}
}

/* Neovim's line_breakcheck counts to 32 before it looks at input. */
int loop_breakcheck(void)
{
	static int count;
	if (loop_int || ++count < 32) return loop_int;
	count = 0;
	run_ready();
	return loop_int;
}

static wchar_t *wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
	wchar_t *r = n > 0 ? malloc((size_t)n * sizeof *r) : NULL;
	if (r) MultiByteToWideChar(CP_UTF8, 0, s, -1, r, n);
	return r;
}

/* argv as one command line that the C runtime's parser splits back into
   the same argv (libuv's quote_cmd_arg): an argument with a blank or a
   quote is quoted, and backslashes before a quote are doubled. */
static char *cmdline(char *const argv[])
{
	size_t n = 1;
	for (int i = 0; argv[i]; i++) n += 2 * strlen(argv[i]) + 3;
	char *s = malloc(n), *o = s;
	if (!s) return NULL;
	for (int i = 0; argv[i]; i++) {
		const char *a = argv[i];
		if (i) *o++ = ' ';
		if (*a && !strpbrk(a, " \t\n\v\"")) {
			size_t k = strlen(a);
			memcpy(o, a, k);
			o += k;
			continue;
		}
		*o++ = '"';
		for (;;) {
			size_t bs = 0;
			while (*a == '\\') { a++; bs++; }
			if (!*a || *a == '"') bs = 2 * bs + (*a == '"');
			for (; bs; bs--) *o++ = '\\';
			if (!*a) break;
			*o++ = *a++;
		}
		*o++ = '"';
	}
	*o = 0;
	return s;
}

/* A named pipe for stream s: ours overlapped, the child's inheritable and
   synchronous, as programs expect of their std handles; overlapped too
   for proc_spawn_ov's stdin and stdout. */
static HANDLE pipe_pair(int s, HANDLE *child, int ov)
{
	static unsigned seq;
	char name[64];
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	snprintf(name, sizeof name, "\\\\.\\pipe\\homeros-%lu-%u", GetCurrentProcessId(), seq++);
	HANDLE h = CreateNamedPipeA(name, (s ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
	                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	*child = CreateFileA(name, s ? GENERIC_WRITE | FILE_READ_ATTRIBUTES : GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, &sa, OPEN_EXISTING, ov ? FILE_FLAG_OVERLAPPED : 0, NULL);
	if (*child != INVALID_HANDLE_VALUE) return h;
	*child = NULL;
	CloseHandle(h);
	return NULL;
}

static struct Proc *proc(int id)
{
	return id >= 0 && id < MAXP && p[id].ph ? &p[id] : NULL;
}

static struct Proc *of(OVERLAPPED *ov, int *s)
{
	INT_PTR k = (INT_PTR)ov->hEvent;
	*s = (int)(k % 3);
	return &p[k / 3];
}

static void ended(struct Proc *c, int s)
{
	CloseHandle(c->h[s]);
	c->h[s] = NULL;
	c->fn(c->ctx, s, NULL, 0);
}

/* Reads at most n bytes that are there now and hands them on; the bytes
   read, 0 on an error. */
static DWORD take(struct Proc *c, int s, DWORD n)
{
	OVERLAPPED o = { 0 };
	DWORD got = 0;
	if (n > sizeof buf) n = sizeof buf;
	if (!ReadFile(c->h[s], buf, n, &got, &o) && (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(c->h[s], &o, &got, TRUE)))
		return 0;
	if (got) c->fn(c->ctx, s, buf, got);
	return got;
}

static void CALLBACK on_read(DWORD e, DWORD n, OVERLAPPED *ov);

/* Called only by loop_once, before its wait: one read per stream per
   pass, as loop.c's one read per poll, so a child that never stops writing
   cannot keep the wait in its completion routines.  0 if the stream ended. */
static int read_next(struct Proc *c, int s)
{
	c->ov[s] = (OVERLAPPED){ .hEvent = (HANDLE)(INT_PTR)((c - p) * 3 + s) };
	if (ReadFileEx(c->h[s], buf, 0, &c->ov[s], on_read)) return c->busy[s] = 1;
	ended(c, s);
	return 0;
}

/* The zero-byte read is done: bytes are there, or the pipe broke (EOF). */
static void CALLBACK on_read(DWORD e, DWORD n, OVERLAPPED *ov)
{
	int s;
	struct Proc *c = of(ov, &s);
	DWORD left = 0;
	(void)n;
	c->busy[s] = 0;
	/* report reads what is left; another child's bytes wait in its pipe
	   for the read after proc_wait */
	if (c->reaping || (only >= 0 && c != &p[only])) return;
	if (e) { ended(c, s); return; }
	if (PeekNamedPipe(c->h[s], NULL, 0, NULL, &left, NULL) && left) take(c, s, left);
}

static void close_in(struct Proc *c)
{
	CloseHandle(c->h[0]);
	c->h[0] = NULL;
	free(c->q);
	free(c->out);
	c->q = c->out = NULL;
	c->qlen = c->qcap = c->outoff = c->outlen = c->outcap = 0;
}

static void CALLBACK on_write(DWORD e, DWORD n, OVERLAPPED *ov);

/* Starts the next write if there is one; with nothing left, a closing
   stdin is closed.  Called only with no write in flight. */
static void write_next(struct Proc *c)
{
	if (c->outoff == c->outlen) {
		if (!c->qlen) { if (c->closing) close_in(c); return; }
		char *b = c->out;
		size_t cap = c->outcap;
		c->out = c->q; c->outcap = c->qcap; c->outlen = c->qlen; c->outoff = 0;
		c->q = b; c->qcap = cap; c->qlen = 0;
	}
	size_t k = c->outlen - c->outoff;
	c->ov[0] = (OVERLAPPED){ .hEvent = (HANDLE)(INT_PTR)((c - p) * 3) };
	if (WriteFileEx(c->h[0], c->out + c->outoff, k > (1u << 30) ? 1u << 30 : (DWORD)k, &c->ov[0], on_write)) c->busy[0] = 1;
	else close_in(c);
}

static void CALLBACK on_write(DWORD e, DWORD n, OVERLAPPED *ov)
{
	int s;
	struct Proc *c = of(ov, &s);
	c->busy[0] = 0;
	if (c->reaping) return;
	if (e) { close_in(c); return; }
	c->outoff += n;
	write_next(c);
}

/* line: the command line, allocated (NULL: out of memory), freed here.
   rows > 0: the child runs on a pseudoconsole of rows x cols (ConPTY, as
   Neovim's conpty_win.c) fed by the stdin and stdout pipes, and gets no
   std handles of its own, which would bypass it */
static int spawn(char *line, const char *dir, int rows, int cols, ProcFn fn, void *ctx)
{
	int id = 0, ok = 0;
	while (id < MAXP && p[id].ph) id++;
	if (id == MAXP) { free(line); return -1; }
	HANDLE mine[3] = { 0 }, theirs[3] = { 0 };
	HPCON pc = NULL;
	/* the child's group (loop.c's setpgroup): proc_kill ends what it
	   started too, as Neovim's os_proc_tree_kill walks the tree on Windows,
	   but a job also holds a grandchild whose parent has exited */
	HANDLE job = CreateJobObjectW(NULL, NULL);
	wchar_t *wl = line ? wide(line) : NULL, *wd = dir ? wide(dir) : NULL;
	for (int s = 0; s < 3 - !!rows; s++) if (!(mine[s] = pipe_pair(s, &theirs[s], cols < 0 && s < 2))) goto out;
	if (!job || !wl || (dir && !wd)) goto out;
	if (rows && CreatePseudoConsole((COORD){ (SHORT)cols, (SHORT)rows }, theirs[0], theirs[1], 0, &pc) != S_OK) { pc = NULL; goto out; }
	/* the editor's own handles are made uninheritable, so these three
	   are all the child gets */
	STARTUPINFOEXW si = { .StartupInfo = { .cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE,
	                      .hStdInput = theirs[0], .hStdOutput = theirs[1], .hStdError = theirs[2] } };
	void *attrs[16];   /* a list of one attribute: 48 bytes on x64 */
	SIZE_T an = sizeof attrs;
	if (rows) {
		/* std handles NULL but given, or a child of an editor whose own are
		   redirected takes those instead of the pseudoconsole's (Windows
		   Terminal's ConPTY sample notes it; Wine 11 does it, measured) */
		si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = NULL;
		si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attrs;
		if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &an)
		    || !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof pc, NULL, NULL)) goto out;
	}
	PROCESS_INFORMATION pi;
	/* no console window per command (Neovim's jobs set windowsHide);
	   suspended until it is in its job, so nothing it starts escapes */
	if (CreateProcessW(NULL, wl, NULL, NULL, !rows, CREATE_SUSPENDED | (rows ? EXTENDED_STARTUPINFO_PRESENT : CREATE_NO_WINDOW), NULL, wd, &si.StartupInfo, &pi)) {
		ok = AssignProcessToJobObject(job, pi.hProcess);
		if (ok) ResumeThread(pi.hThread);
		else { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); }
		CloseHandle(pi.hThread);
	}
	if (rows) DeleteProcThreadAttributeList(si.lpAttributeList);
	if (ok)
		p[id] = (struct Proc){ .ph = pi.hProcess, .job = job, .pc = pc, .h = { mine[0], mine[1], mine[2] }, .fn = fn, .ctx = ctx };
out:
	for (int s = 0; s < 3; s++) {
		if (theirs[s]) CloseHandle(theirs[s]);
		if (!ok && mine[s]) CloseHandle(mine[s]);
	}
	if (!ok && pc) ClosePseudoConsole(pc);
	if (!ok && job) CloseHandle(job);
	free(line); free(wl); free(wd);
	return ok ? id : -1;
}

int proc_start(char *const argv[], const char *dir, int rows, int cols, ProcFn fn, void *ctx)
{
	return spawn(cmdline(argv), dir, rows, cols, fn, ctx);
}

void proc_resize(int id, int rows, int cols)
{
	struct Proc *c = proc(id);
	if (c && c->pc) ResizePseudoConsole(c->pc, (COORD){ (SHORT)cols, (SHORT)rows });
}

/* cmd.exe splits its line itself, not as the CRT does: the command goes
   in as typed, inside the quotes /s strips (Neovim's shellxquote '"' and
   libuv's UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS) */
const char *proc_shell_path(void)
{
	const char *sh = getenv("COMSPEC");
	return sh && *sh ? sh : "cmd.exe";
}

int proc_shell(const char *cmd, const char *dir, ProcFn fn, void *ctx)
{
	const char *sh = proc_shell_path();
	size_t n = strlen(sh) + strlen(cmd) + 16;
	char *line = malloc(n);
	if (line) snprintf(line, n, "\"%s\" /s /c \"%s\"", sh, cmd);
	return spawn(line, dir, 0, 0, fn, ctx);
}

int proc_write(int id, const void *b, size_t n)
{
	struct Proc *c = proc(id);
	if (!c || !c->h[0] || c->closing || c->reaping) return -1;   /* reaping: report closes stdin next, a write begun now would complete on a freed slot */
	if (!n) return 0;
	if (c->qlen + n > c->qcap) {
		size_t cap = c->qcap ? c->qcap : 4096;
		while (cap < c->qlen + n) cap *= 2;
		char *q = realloc(c->q, cap);
		if (!q) return -1;
		c->q = q;
		c->qcap = cap;
	}
	memcpy(c->q + c->qlen, b, n);
	c->qlen += n;
	if (!c->busy[0]) write_next(c);
	return c->h[0] ? 0 : -1;
}

void proc_close(int id)
{
	struct Proc *c = proc(id);
	if (!c || !c->h[0]) return;
	c->closing = 1;
	if (!c->busy[0]) close_in(c);   /* no write in flight: nothing is queued */
}

/* Windows has no SIGTERM for a program without a window, and Neovim's
   jobstop there terminates at once; the job takes the whole tree. */
void proc_kill(int id)
{
	struct Proc *c = proc(id);
	if (c) TerminateJobObject(c->job, KILLED);
}

/* Reports an exited child at once, as loop.c: the reads in flight are
   cancelled (and their APCs run, so the slot is quiet before it is freed),
   what the pipes hold now is read, bounded by what PeekNamedPipe counts,
   since a process left behind may keep writing (Exit.tla "ok"). */
static void report(struct Proc *c)
{
	waited |= c - p == only;
	DWORD code = 0;
	GetExitCodeProcess(c->ph, &code);
	c->reaping = 1;
	for (int s = 0; s < 3; s++) if (c->busy[s]) CancelIoEx(c->h[s], &c->ov[s]);
	for (int s = 0; s < 3; s++) while (c->busy[s]) SleepEx(INFINITE, TRUE);
	for (int s = 1; s < 3; s++) {
		if (!c->h[s]) continue;
		DWORD left = 0, got;
		PeekNamedPipe(c->h[s], NULL, 0, NULL, &left, NULL);
		for (; left && (got = take(c, s, left)); left -= got) {}
		ended(c, s);
	}
	if (c->h[0]) close_in(c);
	/* after our ends are closed, so it cannot wait for us to read */
	if (c->pc) ClosePseudoConsole(c->pc);
	CloseHandle(c->ph);
	CloseHandle(c->job);
	ProcFn fn = c->fn;
	void *ctx = c->ctx;
	*c = (struct Proc){ 0 };
	fn(ctx, 0, NULL, code);
}

/* Ctrl-C reaches loop_int through the watches */
int proc_wait(int id, int ms)
{
	long long end = loop_now() + ms * 1000000LL;
	int r = 0;
	only = id;
	for (waited = !proc(id); !waited; ) {
		long long d = end - loop_now();
		if (!r && (d <= 0 || loop_int)) { proc_kill(id); r = -1; }
		loop_once(r ? -1 : (int)(d / 1000000) + 1);
	}
	only = -1;
	return r;
}

int loop_once(int timeout_ms)
{
	HANDLE h[MAXW + MAXP];
	int who[MAXW + MAXP], n = 0;   /* a watch's slot, or -1 for a child */
	DWORD mask = 0;
	for (int i = 0; i < MAXW; i++) {
		if (!w[i].used) continue;
		if (w[i].pend) timeout_ms = 0;
		if (w[i].fd == LOOP_MSGQ) mask = QS_ALLINPUT;
		else { h[n] = H(w[i].fd); who[n++] = i; }
	}
	for (int i = 0; i < MAXP; i++) {
		if (!p[i].ph || (only >= 0 && i != only)) continue;
		h[n] = p[i].ph; who[n++] = -1;
		for (int s = 1; s < 3; s++)
			if (p[i].h[s] && !p[i].busy[s] && !read_next(&p[i], s)) timeout_ms = 0;   /* its end was reported: the caller may have work */
	}
	if (only < 0) timeout_ms = timer_wait(-1, timeout_ms);
	DWORD r = MsgWaitForMultipleObjectsEx((DWORD)n, h, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms, mask, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
	if (r == WAIT_FAILED) return -1;
	if (r < WAIT_OBJECT_0 + (DWORD)n && who[r - WAIT_OBJECT_0] >= 0) w[who[r - WAIT_OBJECT_0]].pend = 1;   /* the wait took its signal */
	run_ready();   /* as poll, every ready watch, not only the one the wait names */
	if (only < 0) timer_run();
	for (int i = 0; i < MAXP; i++)
		if (p[i].ph && (only < 0 || i == only) && WaitForSingleObject(p[i].ph, 0) == WAIT_OBJECT_0) report(&p[i]);
	return 0;
}
#endif
