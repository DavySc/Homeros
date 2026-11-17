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

int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags)
{
	const char *x = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	int k = x && *x ? snprintf(out, outsz, "%s/homeros/%s/", x, kind)
	      : snprintf(out, outsz, "%s/.local/state/homeros/%s/", home ? home : "", kind);
	if (k < 0 || (size_t)k >= outsz || abs[0] != '/' || (size_t)k + strlen(abs) >= outsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	for (size_t i = 0; abs[i]; i++) out[k + i] = abs[i] == '/' ? '%' : abs[i];
	out[k + strlen(abs)] = 0;
	int fd = open(out, flags | O_CLOEXEC, 0600);
	if (fd >= 0 || errno != ENOENT) return fd;
	/* only the first time: mkdir -p, private, since the journal holds unsaved text */
	for (char *p = strchr(out + 1, '/'); p && p < out + k; p = strchr(p + 1, '/')) {
		*p = 0;
		int r = mkdir(out, 0700);
		*p = '/';
		if (r && errno != EEXIST) return -1;
	}
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
	char dir[PATH_MAX], base[PATH_MAX], d[PATH_MAX];
	if (realpath(path, out)) return 0;
	if (errno != ENOENT || strlen(path) >= PATH_MAX) return -1;
	/* a new file: its directory exists */
	strcpy(dir, path);
	strcpy(base, path);
	if (!realpath(dirname(dir), d)) return -1;
	if (snprintf(out, PATH_MAX, "%s/%s", strcmp(d, "/") ? d : "", basename(base)) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
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
