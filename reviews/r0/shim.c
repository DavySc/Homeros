/* Stands in for another program that rewrites the file right after our
   exchange returns (before file_save takes the new identity). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
int renameat2(int a, const char *o, int b, const char *n, unsigned f)
{
	int (*real)(int, const char *, int, const char *, unsigned) = dlsym(RTLD_NEXT, "renameat2");
	int r = real(a, o, b, n, f);
	if (!r && getenv("OUTSIDE") && !unsetenv("OUTSIDE")) {
		int fd = open(n, O_WRONLY | O_TRUNC);
		if (write(fd, "outside version\n", 16) != 16) abort();
		close(fd);
	}
	return r;
}
