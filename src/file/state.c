#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "state.h"

int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags)
{
	const char *x = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	int k = x && *x ? snprintf(out, outsz, "%s/homeros/%s/", x, kind)
	      : snprintf(out, outsz, "%s/.local/state/homeros/%s/", home ? home : "", kind);
	if (k < 0 || (size_t)k >= outsz || abs[0] != '/') { errno = ENAMETOOLONG; return -1; }
	/* mkdir -p, private: the journal holds unsaved text */
	for (char *p = strchr(out + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = 0;
		int r = mkdir(out, 0700);
		*p = '/';
		if (r && errno != EEXIST) return -1;
	}
	if ((size_t)k + strlen(abs) >= outsz) { errno = ENAMETOOLONG; return -1; }
	for (size_t i = 0; abs[i]; i++) out[k + i] = abs[i] == '/' ? '%' : abs[i];
	out[k + strlen(abs)] = 0;
	return open(out, flags | O_CLOEXEC, 0600);
}
