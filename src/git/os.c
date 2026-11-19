/* The POSIX half of package 17: watching the repository with inotify
   (Git.tla's Outside), paths, the trash.  win.c is the Windows half. */
#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "loop.h"
#include "gitint.h"

extern char **environ;

const char *const git_opener = "xdg-open";

/* one inotify instance per repository; wd numbers name their directory */
struct Watch { int fd; char **path; size_t n; };

static void watch_dir(Repo *r, const char *path, int depth)
{
	struct Watch *w = r->wt;
	if (!w || depth > 64) return;
	int wd = inotify_add_watch(w->fd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM
	                           | IN_MOVED_TO | IN_ATTRIB | IN_CLOSE_WRITE | IN_ONLYDIR);
	if (wd < 0) return;
	if (wd >= (int)w->n) {
		size_t n = (size_t)wd + 64;
		char **q = realloc(w->path, n * sizeof *q);
		if (!q) return;
		for (size_t i = w->n; i < n; i++) q[i] = NULL;
		w->path = q; w->n = n;
	}
	free(w->path[wd]);
	w->path[wd] = strdup(path);
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *de;
	char sub[PATH_MAX];
	while ((de = readdir(d))) {
		/* .git: the common directory has its own walk */
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, ".git")) continue;
		if (de->d_type != DT_DIR && de->d_type != DT_UNKNOWN) continue;
		snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
		struct stat st;
		if (de->d_type == DT_UNKNOWN && (lstat(sub, &st) || !S_ISDIR(st.st_mode))) continue;
		if (git_counts(r, sub)) watch_dir(r, sub, depth + 1);
	}
	closedir(d);
}

/* Reads every queued event: each one that counts is a change */
void git_drain(Repo *r)
{
	struct Watch *w = r->wt;
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	if (!w) return;
	while ((n = read(w->fd, buf, sizeof buf)) > 0) {
		for (char *p = buf; p < buf + n; ) {
			struct inotify_event *ev = (struct inotify_event *)p;
			p += sizeof *ev + ev->len;
			const char *dir = ev->wd >= 0 && (size_t)ev->wd < w->n ? w->path[ev->wd] : NULL;
			if (!dir) continue;
			if (ev->mask & IN_IGNORED) { free(w->path[ev->wd]); w->path[ev->wd] = NULL; continue; }
			char path[PATH_MAX];
			snprintf(path, sizeof path, "%s/%s", dir, ev->len ? ev->name : "");
			if (!git_counts(r, path)) continue;
			if ((ev->mask & (IN_CREATE | IN_MOVED_TO)) && (ev->mask & IN_ISDIR)) watch_dir(r, path, 0);
			r->ver++;
			r->dirty = 1;
		}
	}
}

void git_watch(Repo *r)
{
	struct Watch *w = calloc(1, sizeof *w);
	if (!w || (w->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0) { free(w); return; }
	r->wt = w;
	loop_watch(w->fd, git_changed, r);
	watch_dir(r, r->top, 0);
	watch_dir(r, r->common, 0);
}

void git_unwatch(Repo *r)
{
	struct Watch *w = r->wt;
	if (!w) return;
	loop_unwatch(w->fd);
	close(w->fd);
	for (size_t i = 0; i < w->n; i++) free(w->path[i]);
	free(w->path);
	free(w);
	r->wt = NULL;
}

int git_real(const char *path, char *out) { return realpath(path, out) ? 0 : -1; }

/* rename-file across file systems: copy, then delete (mv does both, for a
   directory too); waited for, as Emacs waits */
static int move(const char *src, const char *dst)
{
	char *argv[] = { "mv", "-f", "--", (char *)src, (char *)dst, NULL };
	pid_t pid;
	int st;
	if (posix_spawnp(&pid, "mv", NULL, NULL, argv, environ)) return -1;
	while (waitpid(pid, &st, 0) < 0) if (errno != EINTR) return -1;
	return WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

/* Emacs's move-file-to-trash on GNU/Linux: the freedesktop.org trash */
int git_trash(const char *src)
{
	char dir[PATH_MAX], dst[3 * PATH_MAX], info[4 * PATH_MAX];
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/Trash", xdg);
	else snprintf(dir, sizeof dir, "%s/.local/share/Trash", home ? home : "");
	char d2[PATH_MAX + 8];
	/* make-directory with parents */
	for (char *s = dir + 1; (s = strchr(s, '/')); s++) { *s = 0; mkdir(dir, 0700); *s = '/'; }
	snprintf(d2, sizeof d2, "%s/files", dir); mkdir(dir, 0700); mkdir(d2, 0700);
	snprintf(d2, sizeof d2, "%s/info", dir); mkdir(d2, 0700);
	const char *base = strrchr(src, '/') + 1;
	for (int k = 0; k < 1000; k++) {
		if (k) snprintf(dst, sizeof dst, "%s/files/%s.~%d~", dir, base, k);
		else snprintf(dst, sizeof dst, "%s/files/%s", dir, base);
		snprintf(info, sizeof info, "%s/info/%s.trashinfo", dir, strrchr(dst, '/') + 1);
		FILE *f = fopen(info, "wx");
		if (!f) { if (errno == EEXIST) continue; return -1; }
		char when[32];
		time_t t = time(NULL);
		strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", localtime(&t));
		fputs("[Trash Info]\nPath=", f);
		for (const unsigned char *s = (const unsigned char *)src; *s; s++)
			if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("/-_.~", *s)) fputc(*s, f);
			else fprintf(f, "%%%02X", *s);
		fprintf(f, "\nDeletionDate=%s\n", when);
		fclose(f);
		if (rename(src, dst) && (errno != EXDEV || move(src, dst))) { remove(info); return -1; }
		return 0;
	}
	return -1;
}
#endif
