/* :make (tools.h): Neovim's :make and quickfix list, its output shown as
   it comes in a buffer of its own as Emacs's M-x compile shows it, and run
   again on every write while that buffer is in a window (focus's runner,
   github.com/jamii/focus, focus-core/src/page/runner.rs).  One command at a
   time; the shell runs it in the current directory, as :make does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "tools.h"
#include "core.h"
#include "loop.h"
#include "make.h"
#include "match.h"
#include "str.h"
#include "text.h"

/* An error: its file and message in the arena, where it points, and the
   output line that reported it (0-based, in the make buffer). */
typedef struct { size_t file, msg; long line, col; size_t at; } Err;

static struct {
	Editor *e;
	char *cmd;                 /* the last command: :make alone and every write run it */
	int proc, gen, nr;         /* the command running (-1 none), its generation, the buffer */
	Str part, a;               /* the output line not ended; the arena */
	Err *q; size_t nq, cap;    /* the quickfix list */
	long cur;                  /* the entry ]q and [q left, -1 none yet */
} M = { .proc = -1, .cur = -1 };

static Buf *buf(void) { return M.nr ? core_buf_find(M.e, M.nr) : NULL; }

static void put(const char *s, size_t n)
{
	Buf *b = buf();
	if (b) core_buf_append(M.e, b, s, n);
}

/* The error a line reports, as the compilers write them: the first
   path:line[:col] (gcc, clang, rustc's " --> ", Lean, grep -n) or
   path(line[,col]) (MSVC, dotnet) whose file exists, a drive letter
   (C:\x) part of the path.  The rest of the line is its message. */
static int scan(const char *s, size_t n, Err *r)
{
	for (size_t i = 0; i < n; ) {
		size_t f = i;
		if (i + 2 < n && s[i + 1] == ':' && (s[i + 2] == '\\' || s[i + 2] == '/')) i += 2;
		while (i < n && s[i] != ':' && s[i] != '(' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') i++;
		size_t fz = i;
		const char *p = s + i + 1, *e = s + n;
		long l = -1, c = 1;
		if (fz > f && i < n && s[i] == ':') {
			l = match_num(&p, e);
			if (l >= 0 && p + 1 < e && *p == ':' && p[1] >= '0' && p[1] <= '9') { p++; c = match_num(&p, e); }
		} else if (fz > f && i < n && s[i] == '(') {
			l = match_num(&p, e);
			if (l >= 0 && p < e && *p == ',') { p++; c = match_num(&p, e); }
			if (c < 0 || p == e || *p++ != ')') l = -1;
		}
		if (l > 0) {
			char path[4096];
			struct stat st;
			snprintf(path, sizeof path, "%.*s", (int)(fz - f), s + f);
			if (!stat(path, &st) && S_ISREG(st.st_mode)) {
				while (p < e && (*p == ':' || *p == ' ')) p++;
				*r = (Err){ .line = l, .col = c > 0 ? c : 1 };
				r->file = M.a.n;
				if (str_add(&M.a, path, strlen(path) + 1)) return 0;
				r->msg = M.a.n;
				return !str_add(&M.a, p, (size_t)(e - p)) && !str_add(&M.a, "", 1);
			}
		}
		while (i < n && s[i] != ' ' && s[i] != '\t') i++;
		while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
	}
	return 0;
}

static void line(void *ctx, char *s, size_t n)
{
	(void)ctx;
	if (n && s[n - 1] == '\r') s[--n] = 0;    /* Windows' CR LF (quickfix.c's USE_CRNL) */
	Buf *b = buf();
	Err r;
	if (b && scan(s, n, &r)) {
		if (M.nq == M.cap) {
			size_t c = M.cap ? 2 * M.cap : 64;
			Err *q = realloc(M.q, c * sizeof *q);
			if (!q) return;
			M.q = q; M.cap = c;
		}
		r.at = text_lines(b->text);
		M.q[M.nq++] = r;
	}
	s[n] = '\n';
	put(s, n + 1);
}

static void out(void *ctx, int stream, const char *b, size_t n)
{
	if ((intptr_t)ctx != M.gen) return;
	if (stream) { str_lines(&M.part, b, n, line, NULL); return; }
	M.proc = -1;
	if (M.part.n) { char *s = M.part.s; size_t k = M.part.n; M.part.n = 0; line(NULL, s, k); }
	char t[80];
	int k = n ? snprintf(t, sizeof t, "\nCompilation exited abnormally with code %d\n", (int)n)
	          : snprintf(t, sizeof t, "\nCompilation finished\n");
	put(t, (size_t)k);
}

/* Starts M.cmd on an emptied buffer and an empty list */
static int run(void)
{
	Buf *b = buf();
	if (M.proc >= 0) proc_kill(M.proc);
	M.proc = -1;
	M.gen++;
	M.part.n = M.a.n = 0;
	M.nq = 0;
	M.cur = -1;
	text_delete(b->text, 0, text_len(b->text));
	b->flags &= ~(unsigned)CORE_EMPTY;
	for (Win *w = M.e->wins; w; w = w->next) if (w->buf == b) w->line = w->col = 0;
	put(M.cmd, strlen(M.cmd));
	put("\n\n", 2);
	if ((M.proc = proc_shell(M.cmd, NULL, out, (void *)(intptr_t)M.gen)) >= 0) return 0;
	core_msg(M.e, "make: cannot run the shell");
	return -1;
}

int tools_make(Editor *e, const char *cmd)
{
	M.e = e;
	if (cmd && *cmd) {
		char *c = strdup(cmd);
		if (!c) return -1;
		free(M.cmd);
		M.cmd = c;
	}
	if (!M.cmd) { core_msg(e, "E471: Argument required"); return -1; }
	/* the buffer in a window below the current one, the cursor staying
	   (Emacs's display-buffer) */
	if (!buf() || !core_buf_shown(M.e, M.nr)) {
		if (core_split(e, 0, 0)) return -1;
		if (buf() ? core_buf_goto(e, M.nr) : core_edit_file(e, "", 0)) { core_close(e, 1); return -1; }
		M.nr = e->buf->nr;
		e->buf->nonumber = e->buf->nomodifiable = 1;
		core_win_goto(e, 'p', 1);
	}
	return run();
}

const char *tools_make_last(void) { return M.cmd; }

void tools_written(Editor *e, Buf *b)
{
	if (M.cmd && M.e == e && b->nr != M.nr && buf() && core_buf_shown(M.e, M.nr)) run();
}

/* Opens error i, in the previous window when the make buffer is current,
   as the quickfix window does; "(i of n): message" as :cc */
static int jump(Editor *e, size_t i)
{
	if (e->buf->nr == M.nr && core_win_goto(e, 'p', 1) && core_split(e, 0, 0)) return -1;
	Err r = M.q[i];
	M.cur = (long)i;
	/* :e on the current buffer's own name reloads it (E37 with changes);
	   :cnext's buflist_getfile() never reloads a loaded buffer */
	const char *f = M.a.s + r.file;
	if ((!e->buf->path || strcmp(e->buf->path, f)) && core_edit_file(e, f, 0)) return -1;
	core_goto(e, (size_t)r.line - 1, (size_t)r.col - 1);
	core_msg(e, "(%zu of %zu): %s", i + 1, M.nq, M.a.s + r.msg);
	return 0;
}

int tools_qf(Editor *e, long n)
{
	if (!M.nq) { core_msg(e, "E42: No Errors"); return -1; }
	long i = M.cur + n;
	if (i < 0 || i >= (long)M.nq) {
		if (M.cur == (n > 0 ? (long)M.nq - 1 : 0)) { core_msg(e, "E553: No more items"); return -1; }
		i = n > 0 ? (long)M.nq - 1 : 0;
	}
	return jump(e, (size_t)i);
}

/* Enter in the make buffer: the error its line reports (the quickfix
   window's <CR>) */
int make_key(Editor *e, int key)
{
	if (key != '\r' || !M.nr || e->buf->nr != M.nr || e->mode != MODE_NORMAL || e->ks.pending) return 0;
	for (size_t i = 0; i < M.nq; i++) if (M.q[i].at == e->win->line) { jump(e, i); return 1; }
	return 0;
}
