/* LD_PRELOAD: SIGKILL just before the KILL_AT-th writev (the journal's
   appends are the editor's only writev calls).
   build: gcc -shared -fPIC -o kill.so kill.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>
ssize_t writev(int fd, const struct iovec *v, int n)
{
	static ssize_t (*real)(int, const struct iovec *, int);
	static int count;
	if (!real) real = dlsym(RTLD_NEXT, "writev");
	if (getenv("KILL_AT") && ++count == atoi(getenv("KILL_AT"))) kill(getpid(), SIGKILL);
	return real(fd, v, n);
}
