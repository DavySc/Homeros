/* Recent files (shada.h): Neovim's oldfiles part of the ShaDa file.  The
   file itself is read and replaced with file_load and file_save, so it is
   written as safely as any buffer, on both systems. */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <limits.h>
#include <sys/stat.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "shada.h"

#ifdef _WIN32
#undef PATH_MAX   /* MAX_PATH UTF-16 units, as UTF-8 (src/file/state.h) */
#define PATH_MAX (3 * 260)
#endif
#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif
enum { KEEP = 100 };   /* 'shada' '100 */

typedef struct { long long t; char *path; } Old;
static Old *old;        /* v:oldfiles as read at startup */
static size_t nold;
static Old *left;       /* this session: buffers left, by path, at most once each */
static size_t nleft, capleft;
static const Buf *cur;  /* the buffer shada_track saw last, and its path then */
static const char *curpath;
static char curabs[PATH_MAX];

/* Neovim's stdpath("data") or ("state"), as homeros names them, + name */
void shada_stdpath(char *f, size_t n, int state, const char *name)
{
	const char *d = getenv(state ? "XDG_STATE_HOME" : "XDG_DATA_HOME");
#ifdef _WIN32
	/* both are %LOCALAPPDATA%\nvim-data on Windows */
	if (!d || !*d) d = getenv("LOCALAPPDATA");
	snprintf(f, n, "%s\\homeros-data%s", d ? d : "", name);
#else
	const char *h = getenv("HOME");
	if (d && *d) snprintf(f, n, "%s/homeros%s", d, name);
	else snprintf(f, n, "%s/%s/homeros%s", h ? h : "", state ? ".local/state" : ".local/share", name);
#endif
}

static int where(char *out, size_t n)
{
	shada_stdpath(out, n, 1, SEP "oldfiles");
	return strlen(out) + 1 >= n ? -1 : 0;
}

/* Neovim's full name of a buffer (fix_fname()): the directory resolved,
   the last part as typed.  0 ok, -1 not. */
static int full(const char *path, char *out)
{
#ifdef _WIN32
	wchar_t w[PATH_MAX], f[PATH_MAX];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, PATH_MAX)) return -1;
	DWORD k = GetFullPathNameW(w, PATH_MAX, f, NULL);
	if (!k || k >= PATH_MAX) return -1;
	return WideCharToMultiByte(CP_UTF8, 0, f, -1, out, PATH_MAX, NULL, NULL) ? 0 : -1;
#else
	char dir[PATH_MAX];
	const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
	size_t dn = slash ? (size_t)(slash - path) + !(slash - path) : 1;
	if (dn >= sizeof dir) return -1;
	memcpy(dir, slash ? path : ".", dn);
	dir[dn] = 0;
	char d[PATH_MAX];
	if (!realpath(dir, d)) return -1;
	int k = snprintf(out, PATH_MAX, "%s/%s", strcmp(d, "/") ? d : "", base);
	return k < 0 || k >= PATH_MAX ? -1 : 0;
#endif
}

/* shada_removable(): a prefix of the 'shada' r items, ignoring case */
static int removable(const char *p)
{
	static const char *const r[] = { "/tmp/", "/private/" };
	for (size_t i = 0; i < sizeof r / sizeof *r; i++) {
		size_t k = 0;
		while (r[i][k] && (p[k] | 0x20) == r[i][k]) k++;
		if (!r[i][k]) return 1;
	}
	return 0;
}

/* Parses "seconds path" lines into *v (newest first, as written). */
static int parse(const char *d, size_t n, Old **v, size_t *nv)
{
	size_t cap = 0;
	*v = NULL; *nv = 0;
	for (size_t i = 0; i < n;) {
		const char *nl = memchr(d + i, '\n', n - i), *e = nl ? nl : d + n;
		char *sp = memchr(d + i, ' ', (size_t)(e - d - i));
		if (sp && sp + 1 < e) {
			if (*nv == cap) {
				Old *t = realloc(*v, (cap = cap ? 2 * cap : 128) * sizeof *t);
				if (!t) return -1;
				*v = t;
			}
			Old *o = &(*v)[*nv];
			o->t = strtoll(d + i, NULL, 10);
			size_t k = (size_t)(e - sp - 1);
			if (!(o->path = malloc(k + 1))) return -1;
			memcpy(o->path, sp + 1, k);
			o->path[k] = 0;
			(*nv)++;
		}
		i = (size_t)(e - d) + 1;
	}
	return 0;
}

static void drop(Old *v, size_t n)
{
	for (size_t i = 0; i < n; i++) free(v[i].path);
	free(v);
}

int shada_read(void)
{
	char f[PATH_MAX], *d;
	size_t n;
	FileId id;
	if (where(f, sizeof f) || file_load(f, &d, &n, &id)) return 0;
	int r = parse(d, n, &old, &nold);
	free(d);
	return r;
}

const char *shada_oldfile(size_t i) { return i < nold ? old[i].path : NULL; }

/* buffer path left at time t: its entry moves to the newest time */
static void note(const char *path, long long t)
{
	if (removable(path)) return;
	for (size_t i = 0; i < nleft; i++)
		if (!strcmp(left[i].path, path)) { left[i].t = t; return; }
	if (nleft == capleft) {
		Old *v = realloc(left, (capleft = capleft ? 2 * capleft : 16) * sizeof *v);
		if (!v) return;          /* only a recent file is forgotten */
		left = v;
	}
	if ((left[nleft].path = strdup(path))) left[nleft++].t = t;
}

void shada_track(const Editor *e)
{
	if (e->buf == cur && e->buf->path == curpath) return;
	if (cur && *curabs) note(curabs, (long long)time(NULL));
	cur = e->buf;
	curpath = cur->path;
	if (!curpath || full(curpath, curabs)) *curabs = 0;
}

void shada_mkdirs(char *f)
{
	for (char *p = f + 1; *p; p++) {
		if (*p != '/' && *p != '\\') continue;
		char c = *p;
		*p = 0;
#ifdef _WIN32
		_mkdir(f);
#else
		mkdir(f, 0700);
#endif
		*p = c;
	}
}

int shada_write(const Editor *e)
{
	char f[PATH_MAX], p[PATH_MAX], kept[PATH_MAX + 32];
	long long now = (long long)time(NULL);
	shada_track(e);
	for (const Win *w = e->wins; w; w = w->next)
		if (w->buf->path && !full(w->buf->path, p)) note(p, now);
	if (!nleft || where(f, sizeof f)) return -1 + !nleft;
	/* what the file holds now: another editor may have exited meanwhile */
	char *d;
	size_t n, nv, m = nleft;
	FileId id;
	Old *v;
	if (file_load(f, &d, &n, &id)) return -1;
	int r = parse(d, n, &v, &nv);
	free(d);
	Old *all = r ? NULL : malloc((nv + nleft) * sizeof *all);
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
			Old x = all[i];
			size_t j = i;
			for (; j && all[j - 1].t < x.t; j--) all[j] = all[j - 1];
			all[j] = x;
		}
	}
	/* the text, at most KEEP lines */
	size_t len = 0, k = 0;
	char *s = NULL;
	Text *t = NULL;
	if (all) {
		for (size_t i = 0; i < m && i < KEEP; i++) len += (size_t)snprintf(NULL, 0, "%lld %s\n", all[i].t, all[i].path);
		if ((s = malloc(len + 1))) {
			for (size_t i = 0; i < m && i < KEEP; i++) k += (size_t)sprintf(s + k, "%lld %s\n", all[i].t, all[i].path);
			t = text_new(s, len);
		}
	}
	free(s);
	free(all);
	drop(v, nv);
	if (!t) return -1;
	shada_mkdirs(f);
	r = file_save(f, t, len, &id, 1, kept, sizeof kept);
	text_free(t);
	if (r == SAVE_KEPT) remove(kept);   /* another editor's, written since we read */
	return r == SAVE_OK || r == SAVE_KEPT ? 0 : -1;
}
