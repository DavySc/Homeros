/* Files on Linux: load, save (Save2.tla "exchange", or in place with a
   backup where a swap would break the file), and outside changes.  Each save
   step names the model action it implements.  win.c is the Windows twin. */
#ifndef _WIN32
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file.h"
#include "state.h"

static void ident(const struct stat *st, FileId *id)
{
	id->exists = 1;
	id->dev = st->st_dev;
	id->ino = st->st_ino;
	id->size = (unsigned long long)st->st_size;
	id->mtime_ns = (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
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

int file_write_all(int fd, const char *b, size_t n)
{
	for (size_t i = 0; i < n;) {
		ssize_t w = write(fd, b + i, n - i);
		if (w < 0 && errno != EINTR) return -1;
		if (w > 0) i += (size_t)w;
	}
	return 0;
}

/* 0 ok (*w the bytes written), -1 error */
static int write_text(int fd, const Text *t, size_t n, int dos, off_t *w)
{
	char b[1 << 16];
	*w = 0;
	for (size_t pos = 0; pos < n;) {
		size_t k = file_text_read(t, &pos, n, dos, b, sizeof b);
		if (file_write_all(fd, b, k)) return -1;
		*w += (off_t)k;
	}
	return 0;
}

static int copy_fd(int from, int to)
{
	char b[1 << 16];
	for (off_t off = 0;;) {
		ssize_t r = pread(from, b, sizeof b, off);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) return (int)r;
		if (file_write_all(to, b, (size_t)r)) return -1;
		off += r;
	}
}

/* Save.tla's in-place variant (torn on a crash), made safe the way Neovim's
   backupcopy=yes with writebackup is: the original is copied to a backup and
   made durable first, and the backup is removed only once the new text is.
   Used where a swap would break the file: hard links (the swap would split
   them), a directory we cannot create the temp in, an owner we cannot give
   the temp.  The identity is checked again by descriptor after the copy, so
   an outside write during the copy is refused rather than lost.  An outside
   rename during it leaves the outsider's file at the path and ours at the
   old inode (the other links): SAVE_OK, nothing lost, and the identity kept
   is the old inode's, so the next :w is refused. */
static int save_in_place(const char *real, const Text *t, size_t n, int dos, FileId *id,
                         int force, char *kept, size_t keptsz)
{
	char bak[PATH_MAX];
	off_t w;
	struct stat st;
	FileId before, after;
	int fd = open(real, O_RDWR | O_CLOEXEC);
	if (fd < 0) return SAVE_ERR;
	if (fstat(fd, &st)) goto err;
	ident(&st, &before);
	if (!force && !file_same(id, &before)) { close(fd); return SAVE_CHANGED; }
	int b = state_open("backup", real, bak, sizeof bak, O_WRONLY | O_CREAT | O_TRUNC);
	/* no backup (a name too long, a full or unwritable state directory):
	   refused, but :w! writes without one, as Neovim's E510 */
	int backed = b >= 0;
	if (!backed && !force) goto err;
	if (backed) {
		if (copy_fd(fd, b) || fsync(b)) { int e = errno; close(b); unlink(bak); close(fd); errno = e; return SAVE_ERR; }
		close(b);
		state_sync_dir(bak);   /* the backup's name durable too, before the recheck: the window after it stays short */
		if (fstat(fd, &st)) { int e = errno; unlink(bak); close(fd); errno = e; return SAVE_ERR; }
		ident(&st, &after);
		if (!force && !file_same(&after, &before)) { unlink(bak); close(fd); return SAVE_CHANGED; }
	}
	/* Trunc and Fill: write first, cut after, so the file is never empty */
	if (write_text(fd, t, n, dos, &w) == 0 && ftruncate(fd, w) == 0 && fsync(fd) == 0 && fstat(fd, &st) == 0) {
		ident(&st, id);
		close(fd);
		if (backed) unlink(bak);
		return SAVE_OK;
	}
	/* torn: put the original back from the backup; keep the backup if that fails too */
	int e = errno;
	if (backed) b = open(bak, O_RDONLY | O_CLOEXEC);
	if (b >= 0 && lseek(fd, 0, SEEK_SET) == 0 && copy_fd(b, fd) == 0
	    && ftruncate(fd, (off_t)before.size) == 0 && fsync(fd) == 0) unlink(bak);
	else if (backed) snprintf(kept, keptsz, "%s", bak);
	if (b >= 0) close(b);
	close(fd);
	errno = e;
	return SAVE_ERR;
err:
	{ int e2 = errno; close(fd); errno = e2; }
	return SAVE_ERR;
}

/* Neovim writes through a symlink to its target, and creates a target that
   does not exist yet: follow the links by hand where realpath fails. */
static int target(const char *path, char *real)
{
	char l[PATH_MAX];
	if (realpath(path, real)) return 0;
	if (errno != ENOENT || strlen(path) >= PATH_MAX) return -1;
	strcpy(real, path);
	for (int hops = 0; hops < 40; hops++) {   /* Linux's MAXSYMLINKS */
		ssize_t k = readlink(real, l, sizeof l);
		if (k < 0) return errno == EINVAL || errno == ENOENT ? 0 : -1;
		char *s = strrchr(real, '/');
		size_t d = l[0] == '/' || !s ? 0 : (size_t)(s + 1 - real);   /* relative to the link's directory */
		if (d + (size_t)k >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
		memcpy(real + d, l, (size_t)k);
		real[d + (size_t)k] = 0;
	}
	errno = ELOOP;
	return -1;
}

int file_save(const char *path, const Text *t, size_t n, int dos, FileId *id,
              int force, char *kept, size_t keptsz)
{
	off_t w;
	char real[PATH_MAX], dir[PATH_MAX], base[PATH_MAX], tmp[PATH_MAX + 32];
	struct stat st;
	FileId now = { 0 }, mine;
	if (target(path, real)) return SAVE_ERR;
	if (!stat(real, &st)) ident(&st, &now);
	else if (errno != ENOENT) return SAVE_ERR;
	/* Check: an outside change since load is never overwritten unasked */
	if (!force && id->exists && !file_same(id, &now)) return SAVE_CHANGED;
	if (!force && !id->exists && now.exists) return SAVE_CHANGED;
	if (now.exists && st.st_nlink > 1) return save_in_place(real, t, n, dos, id, force, kept, keptsz);

	/* dirname() and basename() may return static storage or change their
	   argument, so each works on its own copy and its result is used */
	strcpy(dir, real);
	strcpy(base, real);
	const char *d = dirname(dir);
	snprintf(tmp, sizeof tmp, "%s/.%s.homeros-XXXXXX", d, basename(base));
	int fd = mkstemp(tmp);
	if (fd < 0) {
		/* a directory we may not write in, holding a file we may */
		if (now.exists && (errno == EACCES || errno == EPERM) && !access(real, W_OK))
			return save_in_place(real, t, n, dos, id, force, kept, keptsz);
		return SAVE_ERR;
	}
	if (now.exists) {
		/* the temp must be the file's: if its owner cannot be kept, write in place */
		if (fchmod(fd, st.st_mode & 07777) || ((st.st_uid != geteuid() || st.st_gid != getegid())
		    && fchown(fd, st.st_uid, st.st_gid))) {
			close(fd);
			unlink(tmp);
			return save_in_place(real, t, n, dos, id, force, kept, keptsz);
		}
	} else { mode_t m = umask(0); umask(m); fchmod(fd, 0666 & ~m); }   /* as open(O_CREAT, 0666) */
	/* temp written and fsynced before the swap: WholeAfterCrash needs the
	   data durable before the name points at it (Save2_nofsync fails).  Its
	   identity is taken here, by descriptor: the swap changes none of inode,
	   size or mtime, and a stat by path after the swap could see an outside
	   write as ours (Save2's Rename sets seen in the same step). */
	if (write_text(fd, t, n, dos, &w) || fsync(fd) || fstat(fd, &st)) { int e = errno; close(fd); unlink(tmp); errno = e; return SAVE_ERR; }
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
			if (file_same(&out, &now)) unlink(tmp);
			else { r = SAVE_KEPT; snprintf(kept, keptsz, "%s", tmp); }
		}
	} else if (force ? rename(tmp, real) : renameat2(AT_FDCWD, tmp, AT_FDCWD, real, RENAME_NOREPLACE)) {
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

/* The directory, not the file: a file replaced by rename is a new inode,
   which a watch on the old one would never report.  One inotify instance for
   the process, never closed: closing one waits for the kernel's SRCU grace
   period, 9.5 ms a time here (measured, 200 runs), which made every buffer
   close and each fuzz_keys run 100 times slower. */
int file_watch(const char *path)
{
	static int fd = -1;
	char real[PATH_MAX], dir[PATH_MAX];
	if (!realpath(path, real)) {
		if (errno != ENOENT || strlen(path) >= sizeof real) return -1;
		strcpy(real, path);
	}
	strcpy(dir, real);
	if (fd < 0 && (fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0) return -1;
	if (inotify_add_watch(fd, dirname(dir), IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE
	                      | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF) < 0) return -1;
	return fd;
}

int file_watch_read(int fd, const char *path, const FileId *id)
{
	char b[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	struct stat st;
	FileId now = { 0 };
	for (;;) {
		ssize_t r = read(fd, b, sizeof b);
		if (r > 0) continue;
		if (r < 0 && errno == EINTR) continue;
		if (r < 0 && errno != EAGAIN) return -1;
		break;
	}
	if (!stat(path, &st)) ident(&st, &now);
	else if (errno != ENOENT) return -1;
	return !file_same(id, &now);
}
#endif
