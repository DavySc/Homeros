/* Reference files for Linux.  file_save is Save2.tla's "exchange" variant;
   each step names the model action it implements. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file.h"

static void ident(const struct stat *st, FileId *id)
{
	id->exists = 1;
	id->dev = st->st_dev;
	id->ino = st->st_ino;
	id->size = (unsigned long long)st->st_size;
	id->mtime_ns = (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

static int same(const FileId *a, const FileId *b)
{
	return a->exists == b->exists && (!a->exists || (a->dev == b->dev && a->ino == b->ino
	       && a->size == b->size && a->mtime_ns == b->mtime_ns));
}

int file_load(const char *path, char **data, size_t *n, FileId *id)
{
	struct stat st;
	*data = NULL; *n = 0;
	memset(id, 0, sizeof *id);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
		int e = S_ISREG(st.st_mode) ? errno : EINVAL;
		close(fd);
		errno = e;
		return -1;
	}
	/* the identity before the first read: a write during the read then shows
	   as a change at save time instead of being folded into "ours" */
	ident(&st, id);
	size_t cap = (size_t)st.st_size + 1, len = 0;
	char *b = malloc(cap);
	for (ssize_t r = 1; b && r > 0;) {   /* read to EOF: the size may have changed */
		if (len == cap) { char *nb = realloc(b, cap *= 2); if (!nb) { free(b); b = NULL; break; } b = nb; }
		r = read(fd, b + len, cap - len);
		if (r < 0 && errno == EINTR) r = 1;
		else if (r < 0) { free(b); b = NULL; }
		else len += (size_t)r;
	}
	close(fd);
	if (!b) { memset(id, 0, sizeof *id); return -1; }
	*data = b; *n = len;
	return 0;
}

static int write_all(int fd, const Text *t, size_t n)
{
	char b[1 << 16];
	for (size_t pos = 0; pos < n;) {
		size_t k = text_read(t, pos, b, n - pos < sizeof b ? n - pos : sizeof b);
		for (size_t i = 0; i < k;) {
			ssize_t w = write(fd, b + i, k - i);
			if (w < 0 && errno != EINTR) return -1;
			if (w > 0) i += (size_t)w;
		}
		pos += k;
	}
	return 0;
}

int file_save(const char *path, const Text *t, size_t n, FileId *id, int force,
              char *kept, size_t keptsz)
{
	char real[PATH_MAX], dir[PATH_MAX], base[PATH_MAX], tmp[PATH_MAX + 32];
	struct stat st;
	FileId now = { 0 }, mine;
	/* write through a symlink to its target, as Neovim does */
	if (!realpath(path, real)) {
		if (errno != ENOENT || strlen(path) >= sizeof real) return SAVE_ERR;
		strcpy(real, path);
	}
	if (!stat(real, &st)) ident(&st, &now);
	else if (errno != ENOENT) return SAVE_ERR;
	/* Check: an outside change since load is never overwritten unasked */
	if (!force && id->exists && !same(id, &now)) return SAVE_CHANGED;
	if (!force && !id->exists && now.exists) return SAVE_CHANGED;
	if (now.exists && st.st_nlink > 1) { errno = EMLINK; return SAVE_ERR; }   /* package 11: in place with a backup */

	/* dirname() and basename() may return static storage or change their
	   argument, so each works on its own copy and its result is used */
	strcpy(dir, real);
	strcpy(base, real);
	const char *d = dirname(dir);
	snprintf(tmp, sizeof tmp, "%s/.%s.homeros-XXXXXX", d, basename(base));
	int fd = mkstemp(tmp);
	if (fd < 0) return SAVE_ERR;
	if (now.exists) { fchmod(fd, st.st_mode & 07777); if (fchown(fd, st.st_uid, st.st_gid)) {} }
	else { mode_t m = umask(0); umask(m); fchmod(fd, 0666 & ~m); }   /* as open(O_CREAT, 0666) */
	/* temp written and fsynced before the swap: WholeAfterCrash needs the
	   data durable before the name points at it (Save2_nofsync fails).  Its
	   identity is taken here, by descriptor: the swap changes none of inode,
	   size or mtime, and a stat by path after the swap could see an outside
	   write as ours (Save2's Rename sets seen in the same step). */
	if (write_all(fd, t, n) || fsync(fd) || fstat(fd, &st)) { int e = errno; close(fd); unlink(tmp); errno = e; return SAVE_ERR; }
	ident(&st, &mine);
	close(fd);

	int r = SAVE_OK;
	if (now.exists) {
		/* Rename, exchange variant: swap, then look at what came out */
		if (renameat2(AT_FDCWD, tmp, AT_FDCWD, real, RENAME_EXCHANGE)) {
			if (errno != EINVAL && errno != ENOSYS) { int e = errno; unlink(tmp); errno = e; return SAVE_ERR; }
			/* no exchange on this file system: Save.tla's check-then-rename */
			if (rename(tmp, real)) { int e = errno; unlink(tmp); errno = e; return SAVE_ERR; }
		} else {
			FileId out = { 0 };
			if (!lstat(tmp, &st)) ident(&st, &out);
			if (same(&out, &now)) unlink(tmp);
			else { r = SAVE_KEPT; snprintf(kept, keptsz, "%s", tmp); }
		}
	} else if (renameat2(AT_FDCWD, tmp, AT_FDCWD, real, RENAME_NOREPLACE)) {
		int e = errno;
		unlink(tmp);
		errno = e;
		return e == EEXIST ? SAVE_CHANGED : SAVE_ERR;
	}
	int dfd = open(d, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd >= 0) { fsync(dfd); close(dfd); }
	*id = mine;
	return r;
}
