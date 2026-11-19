/* Package 17 against magit: every recorded trace (src/git/traces.txt, made
   by src/git/record/ from Emacs 31, magit 4402e97 and evil-collection) is
   replayed on a fresh scratch repository (src/git/mkrepo.sh): after each
   step the shown buffer's lines, the cursor's row, the echo area and the
   repository's state (status, refs, HEAD, log, stash list, both diffs) must
   be magit's.  Divergences are listed in div[] with why.  The views run
   without core buffers (git_headless: ref/core.c has one buffer), so this
   test moves the cursor for the Vim motions git leaves to keys (j k G) and
   types the commit message.  Then: a refresh runs at most 5 git processes,
   and reads take no index.lock (a refresh with index.lock held changes
   nothing and fails nowhere). */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "git.h"
#include "keyname.h"
#include "keys.h"
#include "loop.h"
#include "../src/git/gitint.h"

static char tmp[PATH_MAX], repo[PATH_MAX + 8], gitlog[PATH_MAX + 8], realgit[PATH_MAX];
static int maxproc;
static Editor e;
static int fails;

static void fail(const char *sc, const char *keys, const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s, after '%s': ", sc, keys);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fails++;
}

static char *unesc(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++) {
		if (*p == '\\' && p[1]) { p++; *o++ = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; }
		else *o++ = *p;
	}
	*o = 0;
	return s;
}

static char *run(const char *fmt, ...)
{
	char cmd[8192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	FILE *p = popen(cmd, "r");
	CHECK(p);
	size_t n = 0, cap = 4096;
	char *s = malloc(cap);
	CHECK(s);
	size_t k;
	while ((k = fread(s + n, 1, cap - n - 1, p)) > 0) {
		n += k;
		if (n + 1 == cap) { cap *= 2; s = realloc(s, cap); CHECK(s); }
	}
	s[n] = 0;
	pclose(p);
	return s;
}

/* a script run in the repository, as record.py runs it (sh -e); its exit code */
static char *shell(const char *script)
{
	char path[PATH_MAX + 16];
	snprintf(path, sizeof path, "%s/script", tmp);
	FILE *f = fopen(path, "w");
	CHECK(f);
	fputs(script, f);
	fclose(f);
	return run("cd '%s' && sh -e '%s' >/dev/null 2>&1; echo $?", repo, path);
}

static Repo *R(void) { return git_repos; }

/* Run the loop until nothing runs and nothing waits */
static void settle(void)
{
	for (int i = 0; i < 400; i++) {
		CHECK(loop_once(i < 2 ? 5 : 50) == 0);
		Repo *r = R();
		/* a commit waiting for its message is idle, as with-editor */
		int busy = r && r->editing != 2 && (r->busy || r->dirty || r->qn || r->editing == 1 || r->editing == 3);
		for (Aux *a = r ? r->aux : NULL; a; a = a->next) busy |= a->busy;
		if (!busy && i >= 2) return;
	}
	{ Repo *r = R(); fprintf(stderr, "busy %d dirty %d qn %d editing %d\n", r ? r->busy : -1, r ? r->dirty : -1, r ? r->qn : -1, r ? r->editing : -1); }
	CHECK(!"the repository never settled");
}

/* The view lines: text split at newlines */
static int vlines(const View *v, char ***out)
{
	int n = 0;
	char **l = malloc((v->nvis + 1) * sizeof *l);
	CHECK(l);
	const char *s = v->text.s ? v->text.s : "";
	for (size_t i = 0; i < v->nvis; i++) {
		const char *nl = strchr(s, '\n');
		size_t len = nl ? (size_t)(nl - s) : strlen(s);
		l[n] = malloc(len + 1);
		memcpy(l[n], s, len);
		l[n][len] = 0;
		while (len && l[n][len - 1] == ' ') l[n][--len] = 0;
		n++;
		s = nl ? nl + 1 : s + len;
	}
	*out = l;
	return n;
}

/* --- a scenario's expectations, as read from the trace ----------------------------------- */

enum { MAXW = 4, MAXL = 64 };
typedef struct { char buf[128]; int sel, row, n; char *line[MAXL]; } TWin;
typedef struct { char *keys; TWin w[MAXW]; int nw; char *echo; } Step;

/* the state's keys and the commands record.py ran for them */
static const char *const SKEY[] = { "cached", "head", "log", "refs", "stash", "status", "worktree" };
static const char *const SCMD[] = { "diff --cached", "symbolic-ref -q HEAD", "log --all --format='%h %p %s'",
	"for-each-ref --format='%(refname) %(objectname:short) %(upstream:short)'", "stash list",
	"status --porcelain=v2 --branch --untracked-files=all", "diff" };

/* magit's echo where ours says the same in other words (each a divergence) */
static const char *echo_of(const char *s)
{
	static const char *const div[][2] = {
		/* Emacs's start-up message */
		{ "For information about GNU Emacs and the GNU system, type C-h C-a.", "" },
		/* no older messages to recover: no log-edit comment ring */
		{ "Type Z Z to finish, Z Q to cancel, and g k and g j to recover older messages", "Type Z Z to finish, Z Q to cancel" },
		{ "Commit canceled.  Message saved to \xe2\x80\x98log-edit-comment-ring\xe2\x80\x99", "Commit canceled." },
	};
	for (size_t i = 0; i < sizeof div / sizeof *div; i++) if (!strcmp(s, div[i][0])) return div[i][1];
	return s;
}


/* the commit message buffer, typed by hand: i text Esc, then ZZ or ZQ */
static struct { int insert, z; char text[256]; size_t n; } msg;

static void message_key(Repo *r, int k)
{
	if (msg.insert) {
		if (k == KEY_ESC) msg.insert = 0;
		else if (msg.n + 1 < sizeof msg.text) msg.text[msg.n++] = (char)k;
		return;
	}
	if (msg.z) {
		msg.z = 0;
		if (k == 'Z' && msg.n) {
			/* typed at the start of the first line (point is at 1,0) */
			char *old = run("cat '%s'", r->editmsg);
			FILE *f = fopen(r->editmsg, "w");
			CHECK(f);
			fwrite(msg.text, 1, msg.n, f);
			fputs(old, f);
			fclose(f);
			free(old);
		}
		git_editor_done(r, k == 'Z');
		msg.n = 0;
		return;
	}
	if (k == 'i') msg.insert = 1;
	else if (k == 'Z') msg.z = 1;
}

/* the Vim motions git passes to keys, on the headless cursor */
static void motion(size_t *cur, const View *v, int k)
{
	if (k == 'j' && *cur + 1 < v->nvis) ++*cur;
	else if (k == 'k' && *cur) --*cur;
	else if (k == 'G' && v->nvis) *cur = v->nvis - 1;
}

static int top_row[2];             /* the window's first line (status, other), from the cursors */

static void compare_view(const char *sc, const Step *st, const TWin *w, View *v, size_t cur, int log, int which)
{
	char **l;
	int n = vlines(v, &l);
	while (n && !l[n - 1][0]) free(l[--n]);
	if (w->row >= 0) top_row[which] = (int)cur - w->row;
	int top = top_row[which] < 0 ? 0 : top_row[which];
	if (w->row >= 0 && (int)cur - top != w->row) fail(sc, st->keys, "cursor on row %d, magit's on %d", (int)cur - top, w->row);
	for (int i = 0; i < w->n || (top + i < n && i < 58); i++) {
		const char *a = top + i < n ? l[top + i] : "", *b = i < w->n ? w->line[i] : "";
		char x[512], y[512];
		snprintf(x, sizeof x, "%s", a);
		snprintf(y, sizeof y, "%s", b);
		if (log) {
			/* the age changes with the day the test runs: up to the author */
			if (strlen(x) > 89) x[89] = 0;
			if (strlen(y) > 89) y[89] = 0;
			for (size_t k = strlen(x); k && x[k - 1] == ' '; ) x[--k] = 0;
			for (size_t k = strlen(y); k && y[k - 1] == ' '; ) y[--k] = 0;
		}
		if (i >= w->n && !x[0]) continue;
		if (strcmp(x, y)) { fail(sc, st->keys, "line %d is \"%s\", magit's \"%s\"", i, x, y); break; }
	}
	for (int i = 0; i < n; i++) free(l[i]);
	free(l);
}

static void check_step(const char *sc, Step *st, const Step *next, char *state[7], int visit_ok, const char *visit_buf, size_t visit_row)
{
	Repo *r = R();
	TWin *w = NULL;
	for (int i = 0; i < st->nw; i++) if (st->w[i].sel) w = &st->w[i];
	int popup = 0, editmsg = 0;
	for (int i = 0; i < st->nw; i++) {
		popup |= !strcmp(st->w[i].buf, " *transient*");
		editmsg |= !strcmp(st->w[i].buf, "COMMIT_EDITMSG");
	}
	if (!w)
		for (int i = 0; i < st->nw; i++) if (strcmp(st->w[i].buf, " *transient*")) { w = &st->w[i]; break; }
	Aux *a = git_ha;
	if (popup) {
		if (git_prompt() != P_POP) fail(sc, st->keys, "no popup open, magit shows a transient");
	} else if (editmsg) {
		if (r->editing != 2) fail(sc, st->keys, "no commit message being edited");
	} else if (w && !strncmp(w->buf, "magit-log:", 10)) {
		if (!a || a->type != T_LOG) fail(sc, st->keys, "not in a log buffer");
		else compare_view(sc, st, w, &a->v, a->cur, 1, 1);
	} else if (w && (!strncmp(w->buf, "magit-diff:", 11) || !strncmp(w->buf, "magit-revision:", 15) || !strncmp(w->buf, "magit-stash:", 12))) {
		if (!a || a->type != T_DIFF) fail(sc, st->keys, "not in a diff buffer");
		else compare_view(sc, st, w, &a->v, a->cur, 0, 1);
	} else if (w && !strncmp(st->keys, "$ ", 2)) {
		/* magit shows the old status until g r; this editor reads it
		   again when the work tree changes (DESIGN.md, Git.tla Outside):
		   it shows now what magit shows after the g r that follows */
		if (next && !strcmp(next->keys, "gr")) {
			Step s2 = *next;
			s2.keys = st->keys;
			compare_view(sc, &s2, &next->w[0], &r->st, r->cur, 0, 0);
		}
	} else if (w && !strncmp(w->buf, "magit:", 6)) {
		if (a) fail(sc, st->keys, "in a log or diff buffer, magit in the status buffer");
		else compare_view(sc, st, w, &r->st, r->cur, 0, 0);
	} else if (w) {
		/* a visited file */
		if (!visit_ok || strcmp(visit_buf, w->buf) || (w->row >= 0 && (size_t)w->row != visit_row))
			fail(sc, st->keys, "visits %s line %zu, magit %s row %d", visit_ok ? visit_buf : "nothing", visit_row, w->buf, w->row);
	}
	if (!popup && !(w && !strncmp(w->buf, "magit", 5) ? 0 : !editmsg && w)) {
		const char *want = echo_of(st->echo);
		char got[sizeof e.msg];
		snprintf(got, sizeof got, "%s", e.msg);
		for (size_t k = strlen(got); k && got[k - 1] == ' '; ) got[--k] = 0;
		if (strcmp(got, want)) fail(sc, st->keys, "echo \"%s\", magit's \"%s\"", e.msg, want);
	}
	for (int k = 0; k < 7; k++) {
		/* without optional locks, so the check itself changes nothing git watches */
		char *got = run("cd '%s' && '%s' --no-optional-locks %s 2>/dev/null", repo, realgit, SCMD[k]);
		if (strcmp(got, state[k])) fail(sc, st->keys, "%s is\n%s\nmagit's\n%s", SKEY[k], got, state[k]);
		free(got);
	}
}

/* Reads take no index.lock: a refresh of a work tree whose index is out of
   date (git status would write it) creates no .git/index.lock */
static void lock_check(void)
{
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1 && touch '%s/a.txt' '%s/b.txt'", repo, repo, repo, repo);
	free(o);
	git_free();
	char d[PATH_MAX + 16];
	snprintf(d, sizeof d, "%s/.git", repo);
	int fd = inotify_init1(IN_NONBLOCK);
	CHECK(fd >= 0 && inotify_add_watch(fd, d, IN_CREATE) >= 0);
	CHECK(git_status(&e, repo) == 0);
	settle();
	git_key(&e, 'g'); git_key(&e, 'r');
	settle();
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	int locks = 0;
	while ((n = read(fd, buf, sizeof buf)) > 0)
		for (char *p = buf; p < buf + n; ) {
			struct inotify_event *ev = (struct inotify_event *)p;
			p += sizeof *ev + ev->len;
			locks += ev->len && !strcmp(ev->name, "index.lock");
		}
	close(fd);
	if (locks) fail("index.lock", "g r", "the refresh took index.lock %d times", locks);
	/* the check sees one: plain git status takes it here */
	o = run("cd '%s' && touch a.txt && '%s' status >/dev/null", repo, realgit);
	free(o);
	git_free();
}

static int nowatch;                /* inotify off: a write's end alone reads the status again */

static void scenario(const char *name, const char *setup, Step *steps, int n, char *states[][7])
{
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo);
	free(o);
	if (setup) { o = shell(setup); CHECK(atoi(o) == 0); free(o); }
	git_free();
	e.msg[0] = 0;
	git_headless = 1;
	o = run(": > '%s'", gitlog);
	free(o);
	CHECK(git_status(&e, repo) == 0);
	if (nowatch) { loop_unwatch(R()->ifd); close(R()->ifd); R()->ifd = -1; }
	settle();
	/* the refresh that opened it: the git processes the shim saw */
	int nproc = 0;
	o = run("cat '%s' 2>/dev/null; : > '%s'", gitlog, gitlog);
	for (char *p = o; *p; p++) nproc += *p == '\n';
	free(o);
	if (nproc > 5) fail(name, "", "the refresh ran %d git processes", nproc);
	if (nproc > maxproc) maxproc = nproc;
	top_row[0] = top_row[1] = 0;
	memset(&msg, 0, sizeof msg);
	for (int s = 0; s < n; s++) {
		Step *st = &steps[s];
		int visit_ok = 0;
		char visit_buf[PATH_MAX] = "";
		size_t visit_row = 0;
		if (!strncmp(st->keys, "$ ", 2)) {
			o = shell(st->keys + 2);
			CHECK(atoi(o) == 0);
			free(o);
		} else
			for (const char *p = st->keys; *p; ) {
				int k = test_key(&p);   /* (package 17's, agreed by thread 0) */
				Repo *r = R();
				if (!git_prompt()) e.msg[0] = 0;   /* Emacs clears the echo area on a key */
				if (r->editing == 2) { message_key(r, k); settle(); continue; }
				Aux *a = git_ha;
				View *v = a ? &a->v : &r->st;
				size_t *cur = a ? &a->cur : &r->cur;
				if (k == KEY_ENTER && !git_prompt())
					visit_ok = !git_visit_target(v, *cur, -1, visit_buf, sizeof visit_buf, &visit_row);
				if (!git_key(&e, k)) motion(cur, v, k);
				settle();
			}
		settle();
		check_step(name, st, s + 1 < n ? &steps[s + 1] : NULL, states[s], visit_ok, visit_buf, visit_row);
	}
	if (!strcmp(name, "discard-untracked")) {
		/* magit-delete-by-moving-to-trash: the freedesktop.org trash */
		const char *d = getenv("XDG_DATA_HOME");
		o = run("cat '%s/Trash/files/c.txt' '%s/Trash/info/c.txt.trashinfo' 2>&1", d, d);
		if (strncmp(o, "new\n[Trash Info]\nPath=/", 23)) fail(name, "x y", "c.txt is not in the trash: %s", o);
		free(o);
	}
	git_free();
}

/* Git.tla ActsOnSeen: a key pressed on a status the work tree has left
   behind does nothing and says so; the status is read again */
/* A question answered after the view was read again acts on nothing
   (Git.tla: the write is tagged when the user chooses).  hunk: the first
   hunk is reverted outside, the refresh moves the cursor onto the second;
   file: a line is appended outside after "Discard ... a.txt?" */
static void prompt_check(int hunk)
{
	const char *name = hunk ? "prompt-hunk" : "prompt-file";
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo);
	free(o);
	git_free();
	CHECK(git_status(&e, repo) == 0);
	settle();
	git_key(&e, 10); git_key(&e, 10);          /* on "modified   a.txt" */
	if (hunk) { git_key(&e, KEY_TAB); git_key(&e, 10); }
	git_key(&e, 'x');
	if (git_prompt() != P_YN) fail(name, "x", "no question");
	o = run("cd '%s' && %s", repo, hunk ? "sed -i s/TWO/two/ a.txt" : "echo more >> a.txt");
	free(o);
	settle();                                  /* the view is read again, the question still open */
	git_key(&e, 'y');
	settle();
	if (strcmp(e.msg, "git: the repository changed since it was shown; nothing done"))
		fail(name, "y", "echo \"%s\"", e.msg);
	o = run("cd '%s' && grep -c '%s' a.txt", repo, hunk ? "ELEVEN" : "more");
	if (strcmp(o, "1\n")) fail(name, "y", "a.txt lost the change nobody was asked about");
	free(o);
	git_free();
}

/* with-editor: the message buffer deleted otherwise than by ZZ or ZQ (its
   number no longer names a buffer) cancels the commit at the next key */
static void gone_check(void)
{
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo);
	free(o);
	git_free();
	CHECK(git_status(&e, repo) == 0);
	settle();
	char *head = run("cd '%s' && '%s' rev-parse HEAD", repo, realgit);
	git_key(&e, 'c'); git_key(&e, 'c');
	for (int i = 0; i < 400 && R()->editing != 2; i++) CHECK(loop_once(20) == 0);
	CHECK(R()->editing == 2);
	R()->msgnr = 1 << 30;
	git_key(&e, 'j');
	settle();
	if (R()->busy || strcmp(e.msg, "Commit canceled.")) fail("message-buffer-gone", "j", "busy %d, echo \"%s\"", R()->busy, e.msg);
	o = run("cd '%s' && '%s' rev-parse HEAD", repo, realgit);
	if (strcmp(o, head)) fail("message-buffer-gone", "j", "committed");
	free(o); free(head);
	git_free();
}

/* git_free with git commit waiting: only git's children are killed (a
   child of another package's in loop slot 0 lives on), and their ends,
   arriving after, touch nothing freed (ASan) */
static int other_ended;
static void on_other(void *ctx, int stream, const char *b, size_t n) { (void)ctx; (void)b; (void)n; other_ended |= stream == 0; }
static void free_check(void)
{
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo);
	free(o);
	git_free();
	char *argv[] = { "sleep", "3", NULL };
	int other = proc_spawn(argv, "/", on_other, NULL);
	CHECK(other >= 0);
	CHECK(git_status(&e, repo) == 0);
	settle();
	git_key(&e, 'c'); git_key(&e, 'c');
	for (int i = 0; i < 400 && R()->editing != 2; i++) CHECK(loop_once(20) == 0);
	CHECK(R()->editing == 2);
	git_free();
	for (int i = 0; i < 25; i++) CHECK(loop_once(20) == 0);
	if (other_ended) fail("git-free", "", "a child that is not git's was killed");
	proc_kill(other);
	while (!other_ended) CHECK(loop_once(50) == 0);
	CHECK(git_status(&e, repo) == 0);         /* the rest runs on a fresh repository */
	git_free();
}

/* a linked work tree reads refs, tags and the stash from the common
   directory (gitrepository-layout's commondir) */
static void worktree_check(void)
{
	char wt[PATH_MAX + 16];
	snprintf(wt, sizeof wt, "%s/wt", tmp);
	char *o = run("rm -rf '%s' '%s' && sh src/git/mkrepo.sh '%s' >/dev/null 2>&1 && cd '%s' && git stash -q && git tag v1 && git worktree add -q '%s' -b side 2>&1",
	              repo, wt, repo, repo, wt);
	free(o);
	git_free();
	CHECK(git_status(&e, wt) == 0);
	settle();
	const View *v = &R()->st;
	int tag = 0, stashes = 0;
	for (size_t k = 0; k < v->nvis; k++) {
		const char *l = v->pool.s + v->ln[v->vis[k]].off;
		tag |= !strncmp(l, "Tag:      v1", 12);
		stashes |= !strncmp(l, "Stashes (1)", 11);
	}
	if (!tag || !stashes) fail("linked-work-tree", "", "Tag header %d, Stashes %d", tag, stashes);
	git_free();
	free(run("rm -rf '%s'", wt));
}

static void fresh(void)
{
	free(run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo));
	git_free();
	CHECK(git_status(&e, repo) == 0);
	settle();
}

/* a diff opened again (unstaged, then staged) before the first read's
   output is in: only the second read's files are shown (the first is
   waited for, not mixed in or shown) */
static void again_check(void)
{
	fresh();
	git_key(&e, 'd'); git_key(&e, 'u');
	git_key(&e, 'd'); git_key(&e, 's');
	settle();
	while (git_ha && git_ha->busy) CHECK(loop_once(20) == 0);
	Str files = { 0 };
	for (int i = 0; git_ha && i < git_ha->v.nsec; i++)
		if (git_ha->v.sec[i].type == T_FILE && git_ha->v.sec[git_ha->v.sec[i].parent].type != T_INFO)
			str_addz(&files, sec_value(&git_ha->v, i)), str_add(&files, " ", 1);
	str_add(&files, "", 1);
	if (strcmp(files.s, "b.txt ")) fail("diff-twice", "d u d s", "files shown: %s, git diff --cached has b.txt", files.s);
	str_free(&files);
	git_free();
}

/* a buried log is not read again after a write: the write's refresh runs
   the status's reads only */
static void bury_check(void)
{
	fresh();
	git_key(&e, 'l'); git_key(&e, 'l');
	settle();
	git_key(&e, 'q');
	git_key(&e, 10); git_key(&e, 10);          /* on "modified   a.txt" */
	free(run(": > '%s'", gitlog));
	git_key(&e, 's');
	settle();
	char *o = run("grep -c . '%s'", gitlog);
	if (atoi(o) > 5) fail("buried-log", "s", "%d git processes for git add and its refresh", atoi(o));
	free(o);
	git_free();
}

/* reads that cannot start (a full process table) say so, never show a
   clean tree */
static int sleeper_ended;
static void on_sleeper(void *ctx, int stream, const char *b, size_t n) { (void)ctx; (void)b; (void)n; sleeper_ended += stream == 0; }
static void full_check(void)
{
	fresh();
	int id[40], k = 0;
	char *argv[] = { "sleep", "5", NULL };
	while (k < 40 && (id[k] = proc_spawn(argv, "/", on_sleeper, NULL)) >= 0) k++;
	CHECK(k > 2);
	proc_kill(id[--k]);                        /* room for status, not for the reads after it */
	while (sleeper_ended < 1) CHECK(loop_once(50) == 0);
	R()->dirty = 1;
	git_schedule(R());
	for (int i = 0; i < 200 && (R()->busy || R()->dirty); i++) CHECK(loop_once(20) == 0);
	if (R()->busy || strcmp(e.msg, "git: cannot run git")) fail("full-process-table", "", "busy %d, echo \"%s\"", R()->busy, e.msg);
	for (int i = 0; i < k; i++) proc_kill(id[i]);
	while (sleeper_ended < k + 1) CHECK(loop_once(50) == 0);
	git_free();
}

static void stale_check(void)
{
	char *o = run("rm -rf '%s' && sh src/git/mkrepo.sh '%s' 2>&1", repo, repo);
	free(o);
	git_free();
	CHECK(git_status(&e, repo) == 0);
	settle();
	git_key(&e, 10); git_key(&e, 10);          /* on "modified   a.txt" */
	o = run("cd '%s' && echo more >> a.txt", repo);
	free(o);
	CHECK(loop_once(50) == 0);                 /* the change is seen, its read not yet done */
	git_key(&e, 's');
	settle();
	if (strcmp(e.msg, "git: the repository changed since it was shown; nothing done"))
		fail("stale", "s", "echo \"%s\"", e.msg);
	o = run("cd '%s' && '%s' --no-optional-locks diff --cached --name-only", repo, realgit);
	if (strcmp(o, "b.txt\n")) fail("stale", "s", "staged: %s", o);
	free(o);
	git_free();
}

int main(void)
{
	snprintf(tmp, sizeof tmp, "/tmp/homeros-git-XXXXXX");
	CHECK(mkdtemp(tmp));
	snprintf(repo, sizeof repo, "%s/repo", tmp);
	char trash[PATH_MAX + 8], scratch[PATH_MAX + 16];
	snprintf(trash, sizeof trash, "%s/data", tmp);
	snprintf(scratch, sizeof scratch, "%s/scratch", tmp);
	FILE *sf = fopen(scratch, "w");
	CHECK(sf);
	fclose(sf);
	/* record.py's environment */
	setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1); setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
	setenv("GIT_AUTHOR_NAME", "Tester", 1); setenv("GIT_AUTHOR_EMAIL", "t@example.com", 1);
	setenv("GIT_COMMITTER_NAME", "Tester", 1); setenv("GIT_COMMITTER_EMAIL", "t@example.com", 1);
	setenv("GIT_AUTHOR_DATE", "2026-01-02T00:00:00Z", 1); setenv("GIT_COMMITTER_DATE", "2026-01-02T00:00:00Z", 1);
	setenv("LANG", "C.UTF-8", 1);
	setenv("XDG_DATA_HOME", trash, 1);
	/* git as the editor runs it, through a shim that logs each run */
	char *g = run("command -v git");
	g[strcspn(g, "\n")] = 0;
	snprintf(realgit, sizeof realgit, "%s", g);
	free(g);
	snprintf(gitlog, sizeof gitlog, "%s/gitlog", tmp);
	char bin[PATH_MAX + 8], shim[PATH_MAX + 16], path[8192];
	snprintf(bin, sizeof bin, "%s/bin", tmp);
	CHECK(!mkdir(bin, 0700));
	snprintf(shim, sizeof shim, "%s/git", bin);
	FILE *sh = fopen(shim, "w");
	CHECK(sh);
	fprintf(sh, "#!/bin/sh\necho \"$*\" >> '%s'\nexec '%s' \"$@\"\n", gitlog, realgit);
	fclose(sh);
	CHECK(!chmod(shim, 0700));
	snprintf(path, sizeof path, "%s:%s", bin, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
	setenv("PATH", path, 1);
	CHECK(core_open(&e, scratch) == 0);
	e.msg[0] = 0;
	e.rows = 60; e.cols = 100;
	git_editor(&e);

	FILE *f = fopen("src/git/traces.txt", "r");
	CHECK(f);
	static char line[1 << 16];
	static Step steps[32];
	static char *states[32][7];
	char *name = NULL, *setup = NULL, *only = getenv("GIT_SCENARIO");
	int n = 0, done = 0;
	char *cur[7] = { 0 };
	for (;;) {
		char *l = fgets(line, sizeof line, f);
		if (l) l[strcspn(l, "\n")] = 0;
		if (!l || l[0] == '@') {
			if (name && (!only || !strcmp(only, name))) { scenario(name, setup, steps, n, states); done++; }
			struct stat a, b;
			if (name && !only && !strcmp(name, "discard-untracked") && !stat("/dev/shm", &a) && !stat(tmp, &b) && a.st_dev != b.st_dev) {
				/* the trash on another file system: copied, then deleted (rename-file) */
				char shm[] = "/dev/shm/hgtrashXXXXXX", *old = strdup(getenv("XDG_DATA_HOME"));
				CHECK(mkdtemp(shm) && old);
				setenv("XDG_DATA_HOME", shm, 1);
				scenario(name, setup, steps, n, states);
				setenv("XDG_DATA_HOME", old, 1);
				free(old);
				free(run("rm -rf '%s'", shm));
			}
			if (name && !only && !strcmp(name, "stage-file")) {
				/* Git.tla Truthful without the watches (the "nowatch" variant) */
				nowatch = 1;
				scenario(name, setup, steps, n, states);
				nowatch = 0;
			}
			for (int s = 0; s < n; s++) {
				free(steps[s].keys); free(steps[s].echo);
				for (int i = 0; i < steps[s].nw; i++) for (int j = 0; j < steps[s].w[i].n; j++) free(steps[s].w[i].line[j]);
				for (int k = 0; k < 7; k++) free(states[s][k]);
			}
			memset(steps, 0, sizeof steps);
			free(name); free(setup);
			name = setup = NULL;
			n = 0;
			for (int k = 0; k < 7; k++) { free(cur[k]); cur[k] = strdup(""); }
			if (!l) break;
			name = strdup(l + 2);
			continue;
		}
		if (l[0] == '$') setup = strdup(unesc(l + 2));
		else if (l[0] == '>') {
			CHECK(n < 32);
			steps[n++].keys = strdup(unesc(l + 2));
			for (int k = 0; k < 7; k++) states[n - 1][k] = strdup(cur[k]);
		} else if (l[0] == 'w') {
			Step *st = &steps[n - 1];
			CHECK(st->nw < MAXW);
			TWin *w = &st->w[st->nw++];
			char *b1 = strchr(l + 2, '|'), *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
			CHECK(b2);
			*b1 = *b2 = 0;
			snprintf(w->buf, sizeof w->buf, "%s", l + 2);
			w->sel = atoi(b1 + 1);
			w->row = b2[1] == '-' ? -1 : atoi(b2 + 1);
		} else if (l[0] == '|') {
			Step *st = &steps[n - 1];
			TWin *w = &st->w[st->nw - 1];
			if (w->n < MAXL) w->line[w->n++] = strdup(unesc(l + 2));
		} else if (l[0] == 'e') steps[n - 1].echo = strdup(unesc(l + 2));
		else if (l[0] == 's') {
			char *k = l + 2, *v = strchr(k, ' ');
			CHECK(v);
			*v++ = 0;
			for (int i = 0; i < 7; i++)
				if (!strcmp(k, SKEY[i])) { free(states[n - 1][i]); states[n - 1][i] = strdup(unesc(v)); free(cur[i]); cur[i] = strdup(v); }
		}
	}
	fclose(f);
	free(name); free(setup);
	for (int k = 0; k < 7; k++) free(cur[k]);
	CHECK(done > 0);
	if (!only) {
		/* the tags trace needs describe: 5 is reached, never passed */
		CHECK(maxproc == 5);
		lock_check();
		stale_check();
		prompt_check(1);
		prompt_check(0);
		gone_check();
		free_check();
		worktree_check();
		again_check();
		bury_check();
		full_check();
	}
	core_free(&e);
	char *o = run("rm -rf '%s'", tmp);
	free(o);
	if (fails) { fprintf(stderr, "%d differences from magit\n", fails); return 1; }
	printf("test_git: %d scenarios as magit\n", done);
	return 0;
}
