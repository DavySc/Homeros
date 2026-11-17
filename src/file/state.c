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
