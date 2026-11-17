/* LD_PRELOAD: an outside writer replaces $VICTIM just before the editor's
   inotify watch is added, that is between file_load and file_watch.
   build: gcc -shared -fPIC -o late.so late.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
int inotify_add_watch(int fd, const char *p, unsigned m)
{
	static int (*real)(int, const char *, unsigned);
	if (!real) real = dlsym(RTLD_NEXT, "inotify_add_watch");
	const char *v = getenv("VICTIM");
	if (v) { FILE *f = fopen(v, "w"); fputs("outside\n", f); fclose(f); }
	return real(fd, p, m);
}
