/* Recent files (shada.h): Neovim's oldfiles part of the ShaDa file.  The
   file itself is read and replaced with file_load and file_save, so it is
   written as safely as any buffer, on both systems. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "core.h"
#include "shada.h"

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif
enum { KEEP = 100 };   /* 'shada' '100 */

static ShadaLine *old;  /* v:oldfiles as read at startup */
static size_t nold;
static ShadaLine *left; /* this session: buffers left, by path, at most once each */
static size_t nleft, capleft;
static const Buf *cur;  /* the buffer shada_track saw last, and its path then */
static const char *curpath;
static char curabs[PATH_MAX];

static int where(char *out, size_t n)
{
	file_stdpath(out, n, 1, SEP "oldfiles");
	return strlen(out) + 1 >= n ? -1 : 0;
}

int shada_load(const char *name, ShadaLine **v, size_t *nv, FileId *id)
{
	char *d;
	size_t n, cap = 0;
	int r = 0;
	*v = NULL; *nv = 0;
	if (file_load(name, &d, &n, id)) return -1;
	for (size_t i = 0; i < n;) {
		const char *nl = memchr(d + i, '\n', n - i), *e = nl ? nl : d + n;
		char *sp = memchr(d + i, ' ', (size_t)(e - d - i));
		if (sp && sp + 1 < e) {
			if (*nv == cap) {
				ShadaLine *t = realloc(*v, (cap = cap ? 2 * cap : 128) * sizeof *t);
				if (!t) { r = -1; break; }
				*v = t;
			}
			ShadaLine *o = &(*v)[*nv];
			size_t k = (size_t)(e - sp - 1);
			if (!(o->path = malloc(k + 1))) { r = -1; break; }
			o->t = strtod(d + i, NULL);
			memcpy(o->path, sp + 1, k);
			o->path[k] = 0;
			(*nv)++;
		}
		i = (size_t)(e - d) + 1;
	}
	free(d);
	return r;
}

int shada_save(const char *name, const ShadaLine *v, size_t nv, FileId *id)
{
	char f[PATH_MAX], kept[PATH_MAX + 32];
	size_t len = 0, k = 0;
	for (size_t i = 0; i < nv; i++) len += (size_t)snprintf(NULL, 0, "%.17g %s\n", v[i].t, v[i].path);
	char *s = malloc(len + 1);
	if (!s) return -1;
	for (size_t i = 0; i < nv; i++) k += (size_t)sprintf(s + k, "%.17g %s\n", v[i].t, v[i].path);
	Text *t = text_new(s, len);
	free(s);
	if (!t) return -1;
	snprintf(f, sizeof f, "%s", name);
	file_mkdirs(f);
	int r = file_save(name, t, len, 0, id, 1, kept, sizeof kept);
	text_free(t);
	if (r == SAVE_KEPT) remove(kept);   /* another editor's, written since we read */
	return r == SAVE_OK || r == SAVE_KEPT ? 0 : -1;
}

static void drop(ShadaLine *v, size_t n)
{
	for (size_t i = 0; i < n; i++) free(v[i].path);
	free(v);
}

int shada_read(void)
{
	char f[PATH_MAX];
	FileId id;
	/* an unreadable file is no recent files, as Neovim carries on without */
	return !where(f, sizeof f) && shada_load(f, &old, &nold, &id) && errno == ENOMEM ? -1 : 0;
}

const char *shada_oldfile(size_t i) { return i < nold ? old[i].path : NULL; }

/* Neovim's shada_removable() with the default 'shada' r/tmp/,r/private/,
   ignoring case on every system.  Not in shada.h: test_shada.c declares it */
int shada_removable(const char *path)
{
	return !strncasecmp(path, "/tmp/", 5) || !strncasecmp(path, "/private/", 9);
}

/* buffer path left at time t: its entry moves to the newest time */
static void note(const char *path, double t)
{
	if (shada_removable(path)) return;
	for (size_t i = 0; i < nleft; i++)
		if (!strcmp(left[i].path, path)) { left[i].t = t; return; }
	if (nleft == capleft) {
		ShadaLine *v = realloc(left, (capleft = capleft ? 2 * capleft : 16) * sizeof *v);
		if (!v) return;          /* only a recent file is forgotten */
		left = v;
	}
	if ((left[nleft].path = strdup(path))) left[nleft++].t = t;
}

void shada_track(const Editor *e)
{
	if (e->buf == cur && e->buf->path == curpath) return;
	if (cur && *curabs) note(curabs, (double)time(NULL));
	cur = e->buf;
	curpath = cur->path;
	if (!curpath || file_full(curpath, curabs)) *curabs = 0;
}

int shada_write(const Editor *e)
{
	char f[PATH_MAX], p[PATH_MAX];
	double now = (double)time(NULL);
	shada_track(e);
	for (const Win *w = e->wins; w; w = w->next)
		if (w->buf->path && !file_full(w->buf->path, p)) note(p, now);
	if (!nleft || where(f, sizeof f)) return -1 + !nleft;
	/* what the file holds now: another editor may have exited meanwhile */
	size_t nv, m = nleft;
	FileId id;
	ShadaLine *v, *all = NULL;
	int r = shada_load(f, &v, &nv, &id);
	if (!r && !(all = malloc((nv + nleft) * sizeof *all))) r = -1;
	if (all) {
		/* ours, then the file's not among them; a file in both keeps the
		   newer time.  Insertion keeps it newest first, stable on equal
		   times, so ours come first then (as written: hypothesis, Neovim's
		   order on equal seconds was not measured) */
		memcpy(all, left, nleft * sizeof *all);
		for (size_t i = 0; i < nv; i++) {
			size_t j = 0;
			while (j < nleft && strcmp(all[j].path, v[i].path)) j++;
			if (j < nleft) { if (v[i].t > all[j].t) all[j].t = v[i].t; }
			else all[m++] = v[i];
		}
		for (size_t i = 1; i < m; i++) {
			ShadaLine x = all[i];
			size_t j = i;
			for (; j && all[j - 1].t < x.t; j--) all[j] = all[j - 1];
			all[j] = x;
		}
		r = shada_save(f, all, m < KEEP ? m : KEEP, &id);
	}
	free(all);
	drop(v, nv);
	return r;
}
