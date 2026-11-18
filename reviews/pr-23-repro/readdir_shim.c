/* Re-review of PR 23 (6fec9e2): an LD_PRELOAD shim that hands out each
   directory's entries in reverse name order, as another file system might.
   test_tools_oil's recording holds the ids oil gave in the order the
   author's file system listed a directory, so it fails under this shim, as
   it failed in CI.
     gcc -shared -fPIC -o shim.so readdir_shim.c -ldl
     LD_PRELOAD="$(gcc -print-file-name=libasan.so) ./shim.so" build/san/test_tools_oil */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
enum { MAXD = 64, MAXE = 8192 };
static struct { DIR *d; struct dirent *e; int n, i; } t[MAXD];
static int cmp(const void *a, const void *b) { return -strcmp(((const struct dirent *)a)->d_name, ((const struct dirent *)b)->d_name); }
struct dirent *readdir(DIR *d)
{
	static struct dirent *(*real)(DIR *);
	if (!real) real = dlsym(RTLD_NEXT, "readdir");
	int k = -1;
	for (int i = 0; i < MAXD; i++) if (t[i].d == d) k = i;
	if (k < 0) {
		for (int i = 0; i < MAXD; i++) if (!t[i].d) k = i;
		if (k < 0) return real(d);
		t[k].d = d; t[k].n = t[k].i = 0;
		t[k].e = malloc(MAXE * sizeof *t[k].e);
		for (struct dirent *e; t[k].n < MAXE && (e = real(d));) t[k].e[t[k].n++] = *e;
		qsort(t[k].e, (size_t)t[k].n, sizeof *t[k].e, cmp);
	}
	return t[k].i < t[k].n ? &t[k].e[t[k].i++] : NULL;
}
int closedir(DIR *d)
{
	static int (*real)(DIR *);
	if (!real) real = dlsym(RTLD_NEXT, "closedir");
	for (int i = 0; i < MAXD; i++) if (t[i].d == d) { free(t[i].e); t[i].d = NULL; }
	return real(d);
}
