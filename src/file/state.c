#ifndef _WIN32   /* win.c is the Windows twin */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "state.h"

void file_stdpath(char *f, size_t n, int which, const char *name)
{
	static const char *const var[] = { "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CONFIG_HOME" };
	static const char *const dir[] = { ".local/share", ".local/state", ".config" };
	const char *d = getenv(var[which]), *h = getenv("HOME");
	if (d && *d) snprintf(f, n, "%s/homeros%s", d, name);
	else snprintf(f, n, "%s/%s/homeros%s", h ? h : "", dir[which], name);
}

void file_mkdirs(char *f)
{
	for (char *p = strchr(f + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = 0;
		mkdir(f, 0700);
		*p = '/';
	}
}

int file_full(const char *path, char *out)
{
	char dir[PATH_MAX], d[PATH_MAX];
	const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
	size_t dn = slash ? (size_t)(slash - path) + !(slash - path) : 1;
	if (dn >= sizeof dir) { errno = ENAMETOOLONG; return -1; }
	memcpy(dir, slash ? path : ".", dn);
	dir[dn] = 0;
	if (!realpath(dir, d)) return -1;
	int k = snprintf(out, PATH_MAX, "%s/%s", strcmp(d, "/") ? d : "", base);
	if (k < 0 || k >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
	return 0;
}

int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags)
{
	char sub[64];
	snprintf(sub, sizeof sub, "/%s/", kind);
	file_stdpath(out, outsz, 1, sub);
	size_t k = strlen(out);
	if (k + 1 >= outsz || abs[0] != '/' || k + strlen(abs) >= outsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	for (size_t i = 0; abs[i]; i++) out[k + i] = abs[i] == '/' ? '%' : abs[i];
	out[k + strlen(abs)] = 0;
	int fd = open(out, flags | O_CLOEXEC, 0600);
	if (fd >= 0 || errno != ENOENT) return fd;
	/* only the first time: mkdir -p, private, since the journal holds unsaved text */
	file_mkdirs(out);
	return open(out, flags | O_CLOEXEC, 0600);
}

int state_sync_dir(const char *name)
{
	char d[PATH_MAX];
	snprintf(d, sizeof d, "%s", name);
	int fd = open(dirname(d), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return -1;
	int r = fsync(fd);
	close(fd);
	return r;
}

int state_abs(const char *path, char *out)
{
	/* a new file (or a dangling link): its directory exists */
	if (realpath(path, out)) return 0;
	return errno == ENOENT ? file_full(path, out) : -1;
}

int state_lock(int fd, const char *name, long long *size)
{
	struct stat a, b;
	/* a lock, not the file's presence, says another editor is live: a
	   crashed one's lock went with its process */
	if (flock(fd, LOCK_EX | LOCK_NB)) return -1;
	/* the holder before us may have removed the name meanwhile */
	if (fstat(fd, &a) || stat(name, &b) || a.st_ino != b.st_ino || a.st_dev != b.st_dev) return 1;
	*size = a.st_size;
	return 0;
}

void state_close(int fd, const char *name)
{
	unlink(name);
	close(fd);
}
#endif
