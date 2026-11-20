/* A new file in a directory that does not exist: does the core open it as a
   named buffer, and what does file_full() say for its path?  Works in its own
   scratch directory.  Run: cp to tests/test_zz_missing_dir.c, then
   make test TESTS=zz_missing_dir (and remove the copy). */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "file.h"
int main(void)
{
	char d[] = "/tmp/pr75-XXXXXX", out[PATH_MAX];
	if (!mkdtemp(d) || chdir(d)) return 1;
	setenv("XDG_STATE_HOME", d, 1);
	Editor e = { 0 };
	int r = core_open(&e, "nodir/new.txt");
	printf("core_open -> %d, path %s, msg: %s\n", r, e.buf && e.buf->path ? e.buf->path : "(null)", e.msg);
	r = file_full("nodir/new.txt", out);
	printf("file_full -> %d (%s)\n", r, r ? strerror(errno) : out);
	core_free(&e);
}
