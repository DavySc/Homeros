/* Integrated magit (DESIGN.md "Git (magit)").  magit 4402e97 with
   evil-collection's keys is the reference; the functions copied are named
   in the comments.  The timing is Git.tla's "ok": one git at a time, a
   write runs only if the repository is still the one the buffer shows
   (else it is dropped with a message), and a finished write or a change
   seen by inotify on .git or the work tree marks the buffer dirty, read
   again when nothing runs.  A refresh runs at most 5 git processes: status
   first (its ahead/behind picks the log's range), then log, diff, diff
   --cached and, only when tags exist, describe; the stash list is read
   from .git/logs/refs/stash.  Reads pass --no-optional-locks, so they never
   take index.lock. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "git.h"
#include "loop.h"
#include "sec.h"
#include "gitint.h"

/* magit-git-global-arguments */
static const char *const GLOBAL[] = { "git", "--no-pager", "--literal-pathspecs", "-c", "core.preloadIndex=true",
	"-c", "log.showSignature=false", "-c", "color.ui=false", "-c", "color.diff=false",
	"-c", "diff.noPrefix=false", NULL };
#define LOGFMT "--format=%m%x1f%h%x1f%D%x1f%s%x1f%p"
/* with-editor's sleeping editor: git runs it with the message file; it
   names the file and waits for a line: "y" finishes, anything else cancels */
#define EDITOR "GIT_EDITOR=f() { printf '\\nHOMEROS-EDIT %s\\n' \"$1\"; read -r r; test \"$r\" = y; }; f"

Repo *git_repos;
static Editor *ed;

/* --- small helpers ---------------------------------------------------- */


static void msg(const char *fmt, const char *a)
{
	if (ed) { core_msg(ed, fmt, a); core_changed = 1; }
}

void cmd_add(Cmd *c, const char *a)
{
	if (c->argc < CMD_MAX - 1 && !str_add(&c->args, a, strlen(a) + 1)) c->argc++;
}

void cmd_git(Cmd *c, int read)
{
	*c = (Cmd){ 0 };
	for (int i = 0; GLOBAL[i]; i++) {
		cmd_add(c, GLOBAL[i]);
		if (i == 0 && read) cmd_add(c, "--no-optional-locks");
	}
}

/* git commit's command: with-editor's sleeping editor in GIT_EDITOR */
void cmd_editor(Cmd *c)
{
	*c = (Cmd){ 0 };
	cmd_add(c, "env");
	cmd_add(c, EDITOR);
	for (int i = 0; GLOBAL[i]; i++) cmd_add(c, GLOBAL[i]);
	c->keepin = 1;
}

void cmd_free(Cmd *c) { str_free(&c->args); str_free(&c->in); c->argc = 0; }

/* --- processes --------------------------------------------------------- */

static void done(Repo *r, int slot);

/* by address among the live ones: after git_free a killed child's end
   still arrives, with its context freed */
int git_live(const void *ctx)
{
	for (Repo *r = git_repos; r; r = r->next) {
		if ((const char *)ctx >= (const char *)r->run && (const char *)ctx < (const char *)(r->run + NSLOT)) return 1;
		for (Aux *a = r->aux; a; a = a->next) if (ctx == a) return 1;
	}
	return 0;
}

static void on_proc(void *ctx, int stream, const char *b, size_t n)
{
	if (!git_live(ctx)) return;
	Run *u = ctx;
	Repo *r = u->r;
	if (stream == 1 && n) {
		str_add(&r->out[u->slot], b, n);
		if (u->slot == SLOT_WRITE && r->editing == 1) git_editor_seen(r);
	} else if (stream == 2 && n) str_add(&r->err, b, n);
	else if (stream == 0) { r->rc[u->slot] = (int)n; r->pid[u->slot] = -1; done(r, u->slot); }
}

int git_spawn(Repo *r, int slot, Cmd *c)
{
	char *argv[CMD_MAX];
	char *p = c->args.s;
	for (int i = 0; i < c->argc; i++, p += strlen(p) + 1) argv[i] = p;
	argv[c->argc] = NULL;
	r->out[slot].n = 0;
	r->run[slot] = (Run){ r, slot };
	int id = proc_spawn(argv, r->top, on_proc, &r->run[slot]);
	if (id < 0) { r->failed = 1; return -1; }
	r->pid[slot] = id;
	r->nproc++;
	r->left++;
	if (c->in.n) proc_write(id, c->in.s, c->in.n);
	if (!c->keepin) proc_close(id);
	return 0;
}

/* --- inotify: Git.tla's Outside ---------------------------------------- */

static int ignored(Repo *r, const char *rel)
{
	/* r->ign holds status's "! path" entries, sorted as git lists them */
	size_t a = 0, z = r->nign;
	while (a < z) {
		size_t m = a + (z - a) / 2;
		int c = strcmp(r->ignp.s + r->ign[m], rel);
		if (!c) return 1;
		if (c < 0) a = m + 1; else z = m;
	}
	return 0;
}

static void watch_dir(Repo *r, const char *path, int depth)
{
	if (r->ifd < 0 || depth > 64) return;
	int wd = inotify_add_watch(r->ifd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM
	                           | IN_MOVED_TO | IN_ATTRIB | IN_CLOSE_WRITE | IN_ONLYDIR);
	if (wd < 0) return;
	if (wd >= (int)r->nwd) {
		size_t n = (size_t)wd + 64;
		char **q = realloc(r->wdpath, n * sizeof *q);
		if (!q) return;
		for (size_t i = r->nwd; i < n; i++) q[i] = NULL;
		r->wdpath = q; r->nwd = n;
	}
	free(r->wdpath[wd]);
	r->wdpath[wd] = strdup(path);
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *de;
	char sub[PATH_MAX];
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, ".git")) continue;
		if (de->d_type != DT_DIR && de->d_type != DT_UNKNOWN) continue;
		snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
		struct stat st;
		if (de->d_type == DT_UNKNOWN && (lstat(sub, &st) || !S_ISDIR(st.st_mode))) continue;
		size_t tl = strlen(r->top);
		if (!strncmp(sub, r->top, tl) && sub[tl] == '/') {
			char rel[PATH_MAX];
			snprintf(rel, sizeof rel, "%s/", sub + tl + 1);
			if (ignored(r, rel)) continue;
		} else if (strncmp(sub, r->common, strlen(r->common))) continue;
		watch_dir(r, sub, depth + 1);
	}
	closedir(d);
}

/* Reads every queued event: each one is a change to the repository. */
void git_drain(Repo *r)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	if (r->ifd < 0) return;
	while ((n = read(r->ifd, buf, sizeof buf)) > 0) {
		for (char *p = buf; p < buf + n; ) {
			struct inotify_event *ev = (struct inotify_event *)p;
			p += sizeof *ev + ev->len;
			const char *dir = ev->wd >= 0 && (size_t)ev->wd < r->nwd ? r->wdpath[ev->wd] : NULL;
			if (!dir) continue;
			if (ev->mask & IN_IGNORED) { free(r->wdpath[ev->wd]); r->wdpath[ev->wd] = NULL; continue; }
			char path[PATH_MAX];
			snprintf(path, sizeof path, "%s/%s", dir, ev->len ? ev->name : "");
			size_t tl = strlen(r->top);
			int ingit = !strncmp(path, r->common, strlen(r->common));
			if (!ingit) {
				if (ev->len && !strcmp(ev->name, ".git")) continue;
				const char *rel = path + tl + 1;
				char reld[PATH_MAX];
				snprintf(reld, sizeof reld, "%s/", rel);
				if (ignored(r, rel) || ignored(r, reld)) continue;
			}
			if ((ev->mask & (IN_CREATE | IN_MOVED_TO)) && (ev->mask & IN_ISDIR)) watch_dir(r, path, 0);
			r->ver++;
			r->dirty = 1;
		}
	}
}

static void on_inotify(void *ctx, int fd)
{
	(void)fd;
	Repo *r = ctx;
	unsigned long v = r->ver;
	git_drain(r);
	if (r->ver != v) git_schedule(r);
}

/* --- refresh ----------------------------------------------------------- */

static int has_tags(Repo *r)
{
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/refs/tags", r->common);
	DIR *d = opendir(p);
	if (d) {
		struct dirent *de;
		int any = 0;
		while (!any && (de = readdir(d))) any = de->d_name[0] != '.';
		closedir(d);
		if (any) return 1;
	}
	snprintf(p, sizeof p, "%s/packed-refs", r->common);
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	char line[1024];
	int any = 0;
	while (!any && fgets(line, sizeof line, f)) any = strstr(line, " refs/tags/") != NULL;
	fclose(f);
	return any;
}

static void start_refresh(Repo *r)
{
	Cmd c;
	git_drain(r);
	r->dirty = 0;
	r->pend = r->ver;
	r->busy = BUSY_REFRESH;
	r->phase = 1;
	r->left = 0;
	r->nproc = 0;
	r->failed = 0;
	cmd_git(&c, 1);
	cmd_add(&c, "status");
	cmd_add(&c, "--porcelain=v2");
	cmd_add(&c, "-z");
	cmd_add(&c, "--branch");
	cmd_add(&c, "--untracked-files=normal");
	cmd_add(&c, "--ignored=traditional");
	if (git_spawn(r, SLOT_STATUS, &c)) { r->busy = 0; msg("git: cannot run git", NULL); }
	cmd_free(&c);
}

/* The log's range from status's ahead/behind (magit-insert-unpushed-to-
   upstream-or-recent, magit-insert-unpulled-from-upstream): one log covers
   the recent, unmerged and unpulled commits and the two headers' subjects. */
static void start_reads(Repo *r)
{
	Branch *b = &r->br;
	Cmd c;
	r->phase = 2;
	for (int s = SLOT_LOG; s < SLOT_WRITE; s++) r->out[s].n = 0;
	r->logmode = LOG_NONE;
	if (b->oid && strcmp(b->oid, "(initial)")) {
		cmd_git(&c, 1);
		cmd_add(&c, "log");
		cmd_add(&c, LOGFMT);
		cmd_add(&c, "--decorate=full");
		if (!b->upstream || !b->ab || (!b->ahead && !b->behind)) {
			cmd_add(&c, "-n10"); cmd_add(&c, "HEAD");
			r->logmode = LOG_RECENT;
		} else if (b->ahead) {
			cmd_add(&c, "--left-right"); cmd_add(&c, "--boundary"); cmd_add(&c, "HEAD...@{upstream}");
			r->logmode = LOG_LR;
		} else {
			/* behind only: what @{upstream} adds, and HEAD's last ten (every
			   commit newer than HEAD~10 is outside ^HEAD~10 when commit dates
			   only grow along parents; hypothesis for skewed clocks) */
			cmd_add(&c, "--ignore-missing"); cmd_add(&c, "@{upstream}"); cmd_add(&c, "^HEAD~10");
			r->logmode = LOG_BEHIND;
		}
		cmd_add(&c, "--");
		git_spawn(r, SLOT_LOG, &c);
		cmd_free(&c);
	}
	for (int k = 0; k < 2; k++) {
		/* magit-insert-unstaged-changes, magit-insert-staged-changes */
		cmd_git(&c, 1);
		cmd_add(&c, "diff");
		if (k) cmd_add(&c, "--cached");
		cmd_add(&c, "--ita-visible-in-index"); cmd_add(&c, "--no-ext-diff");
		cmd_add(&c, "--ignore-submodules=none"); cmd_add(&c, "--no-prefix"); cmd_add(&c, "--");
		git_spawn(r, k ? SLOT_CACHED : SLOT_DIFF, &c);
		cmd_free(&c);
	}
	r->rc[SLOT_DESCRIBE] = 1;
	char p[PATH_MAX + 16];
	snprintf(p, sizeof p, "%s/MERGE_HEAD", r->gitdir);
	FILE *f = fopen(p, "r");
	r->merging[0] = 0;
	if (f) { if (!fgets(r->merging, sizeof r->merging, f)) r->merging[0] = 0; fclose(f); }
	r->merging[strcspn(r->merging, "\n")] = 0;
	if (r->merging[0]) {
		/* magit-insert-merge-log, in describe's place: a refresh stays at
		   5 processes, and the Tag header waits for the merge's end */
		char range[64];
		snprintf(range, sizeof range, "HEAD..%s", r->merging);
		cmd_git(&c, 1);
		cmd_add(&c, "log"); cmd_add(&c, LOGFMT); cmd_add(&c, "--decorate=full"); cmd_add(&c, range); cmd_add(&c, "--");
		git_spawn(r, SLOT_DESCRIBE, &c);
		cmd_free(&c);
	} else if (has_tags(r) && b->oid && strcmp(b->oid, "(initial)")) {
		/* magit-get-current-tag */
		cmd_git(&c, 1);
		cmd_add(&c, "describe"); cmd_add(&c, "--long"); cmd_add(&c, "--tags");
		git_spawn(r, SLOT_DESCRIBE, &c);
		cmd_free(&c);
	}
}

static void done(Repo *r, int slot)
{
	r->left--;
	if (slot == SLOT_WRITE) { git_write_step_done(r); return; }
	if (r->left) return;
	if (r->phase == 1) {
		if (r->rc[SLOT_STATUS] || parse_status(r->out[SLOT_STATUS].s, r->out[SLOT_STATUS].n, &r->br)) {
			r->busy = 0;
			msg("git: status failed", NULL);
			return;
		}
		/* the ignored entries, for the watches */
		r->ignp.n = 0; r->nign = 0;
		const char *s = r->out[SLOT_STATUS].s;
		for (size_t i = 0, n = r->out[SLOT_STATUS].n; i < n; ) {
			const char *p = s + i;
			size_t len = strlen(p);
			i += len + 1;
			if (len > 2 && p[0] == '!' && p[1] == ' ') {
				size_t *q = realloc(r->ign, (r->nign + 1) * sizeof *q);
				if (!q) break;
				r->ign = q;
				r->ign[r->nign++] = r->ignp.n;
				str_add(&r->ignp, p + 2, len - 2 + 1);
			}
		}
		if (!r->watched) { r->watched = 1; watch_dir(r, r->top, 0); watch_dir(r, r->common, 0); }
		start_reads(r);
		if (!r->left) { r->left = 1; done(r, -1); }
		return;
	}
	if (r->failed) {
		/* a missing read would show a clean tree */
		r->busy = 0;
		msg("git: cannot run git", NULL);
		return;
	}
	git_build_status(r);
	r->shown = r->pend;
	r->busy = 0;
	git_show(r, &r->st);
	git_schedule(r);
}

/* Git.tla: a write first, else a refresh when dirty; never two at once */
void git_schedule(Repo *r)
{
	if (r->busy) return;
	if (r->qn) git_start_write(r);
	else if (r->dirty) start_refresh(r);
}

/* --- repositories ---------------------------------------------------------- */

/* The work tree holding dir: the nearest parent with .git (a directory, or
   a file "gitdir: ..." for a linked work tree) */
static int find_top(const char *dir, char **top, char **gitdir)
{
	char p[PATH_MAX], g[3 * PATH_MAX];
	if (!realpath(dir, p)) return -1;
	for (;;) {
		struct stat st;
		snprintf(g, sizeof g, "%s/.git", p);
		if (!stat(g, &st)) {
			if (S_ISREG(st.st_mode)) {
				FILE *f = fopen(g, "r");
				char line[PATH_MAX + 16] = "";
				if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
				line[strcspn(line, "\n")] = 0;
				if (strncmp(line, "gitdir: ", 8)) return -1;
				char rp[PATH_MAX];
				if (line[8] == '/') snprintf(g, sizeof g, "%s", line + 8);
				else snprintf(g, sizeof g, "%s/%s", p, line + 8);
				if (!realpath(g, rp)) return -1;
				snprintf(g, sizeof g, "%s", rp);
			}
			*top = strdup(p);
			*gitdir = strdup(g);
			return *top && *gitdir ? 0 : -1;
		}
		char *s = strrchr(p, '/');
		if (!s || s == p) return -1;
		*s = 0;
	}
}

Repo *git_repo(const char *dir)
{
	char *top, *gitdir;
	if (find_top(dir, &top, &gitdir)) return NULL;
	for (Repo *r = git_repos; r; r = r->next)
		if (!strcmp(r->top, top)) { free(top); free(gitdir); return r; }
	Repo *r = calloc(1, sizeof *r);
	if (!r) { free(top); free(gitdir); return NULL; }
	r->top = top;
	r->gitdir = gitdir;
	/* gitrepository-layout: "commondir" names the shared directory */
	char p[PATH_MAX + 16], line[PATH_MAX], g[2 * PATH_MAX + 2], rp[PATH_MAX];
	snprintf(p, sizeof p, "%s/commondir", gitdir);
	FILE *f = fopen(p, "r");
	line[0] = 0;
	if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
	line[strcspn(line, "\n")] = 0;
	if (line[0] == '/') snprintf(g, sizeof g, "%s", line);
	else snprintf(g, sizeof g, "%s/%s", gitdir, line);
	r->common = strdup(line[0] && realpath(g, rp) ? rp : gitdir);
	if (!r->common) { free(r); free(top); free(gitdir); return NULL; }
	r->st.cache = &r->cache;
	r->ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (r->ifd >= 0) loop_watch(r->ifd, on_inotify, r);
	r->dirty = 1;
	for (int s = 0; s < NSLOT; s++) r->pid[s] = -1;
	r->next = git_repos;
	git_repos = r;
	return r;
}

static void repo_free(Repo *r)
{
	for (int s = 0; s < NSLOT; s++) str_free(&r->out[s]);
	str_free(&r->err);
	str_free(&r->ignp);
	free(r->ign);
	for (size_t i = 0; i < r->nwd; i++) free(r->wdpath[i]);
	free(r->wdpath);
	if (r->ifd >= 0) { loop_unwatch(r->ifd); close(r->ifd); }
	for (int i = 0; i < r->qn; i++) git_write_free(&r->q[(r->qh + i) % QMAX]);
	git_write_free(&r->w);
	view_free(&r->st);
	view_free(&r->old);
	str_free(&r->cache.s);
	branch_free(&r->br);
	for (Aux *a = r->aux, *n; a; a = n) { n = a->next; git_aux_free(a); }
	free(r->editmsg);
	free(r->top); free(r->gitdir); free(r->common);
	free(r);
}

void git_free(void)
{
	for (Repo *r = git_repos, *n; r; r = n) {
		n = r->next;
		for (int s = 0; s < NSLOT; s++) if (r->pid[s] >= 0) proc_kill(r->pid[s]);
		repo_free(r);
	}
	git_repos = NULL;
	git_keys_free();
	ed = NULL;
}

void git_editor(Editor *e) { ed = e; }
Editor *git_ed(void) { return ed; }
