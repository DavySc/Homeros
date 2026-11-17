/* LD_PRELOAD shim: before the STEP-th file-system call the program makes
   (counted while the program has SHIM_ARMED in its environment), an outside writer changes the file $VICTIM:
   MODE=rename (a new inode with other text), inplace (same inode, other
   size), same (same inode, same size).  Independent of the PR's own test,
   which wraps the calls inside the test binary.
   build: gcc -shared -fPIC -o shim.so shim.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int count, busy;
static void outside(const char *name)
{
	const char *v = getenv("VICTIM"), *mode = getenv("MODE"), *step = getenv("STEP");
	if (!getenv("SHIM_ARMED") || busy || !v || !step) return;
	if (++count != atoi(step)) return;
	busy = 1;
	setenv("SHIM_RAN", "1", 1);
	if (getenv("SHIM_TRACE")) fprintf(stderr, "  outside writer before call %d (%s)\n", count, name);
	if (!strcmp(mode, "rename")) {
		char t[4096];
		snprintf(t, sizeof t, "%s.outside", v);
		int fd = syscall(SYS_openat, AT_FDCWD, t, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		syscall(SYS_write, fd, "OUTSIDE by rename\n", 18);
		syscall(SYS_close, fd);
		syscall(SYS_renameat, AT_FDCWD, t, AT_FDCWD, v);
	} else {
		int fd = syscall(SYS_openat, AT_FDCWD, v, O_WRONLY);
		if (!strcmp(mode, "same")) syscall(SYS_write, fd, "OUTSIDE!\n", 9);   /* as long as "original\n" */
		else syscall(SYS_write, fd, "OUTSIDE in place\n", 17);
		syscall(SYS_close, fd);
	}
	busy = 0;
}
#define REAL(name) static __typeof__(name) *real; if (!real) real = dlsym(RTLD_NEXT, #name)
int stat(const char *p, struct stat *s) { REAL(stat); outside("stat"); return real(p, s); }
int lstat(const char *p, struct stat *s) { REAL(lstat); outside("lstat"); return real(p, s); }
int fstat(int fd, struct stat *s) { REAL(fstat); outside("fstat"); return real(fd, s); }
int open(const char *p, int fl, ...) { REAL(open); va_list a; va_start(a, fl); int m = va_arg(a, int); va_end(a); outside("open"); return real(p, fl, m); }
int mkstemp(char *t) { REAL(mkstemp); outside("mkstemp"); return real(t); }
int fchmod(int fd, mode_t m) { REAL(fchmod); outside("fchmod"); return real(fd, m); }
int fchown(int fd, uid_t u, gid_t g) { REAL(fchown); outside("fchown"); return real(fd, u, g); }
ssize_t write(int fd, const void *b, size_t n) { REAL(write); if (fd > 2) outside("write"); return real(fd, b, n); }
ssize_t pread(int fd, void *b, size_t n, off_t o) { REAL(pread); outside("pread"); return real(fd, b, n, o); }
int fsync(int fd) { REAL(fsync); outside("fsync"); return real(fd); }
int close(int fd) { REAL(close); if (fd > 2) outside("close"); return real(fd); }
int ftruncate(int fd, off_t n) { REAL(ftruncate); outside("ftruncate"); return real(fd, n); }
int unlink(const char *p) { REAL(unlink); outside("unlink"); return real(p); }
int rename(const char *a, const char *b) { REAL(rename); outside("rename"); return real(a, b); }
int renameat2(int a, const char *p, int b, const char *q, unsigned f) { REAL(renameat2); outside("renameat2"); return real(a, p, b, q, f); }
int access(const char *p, int m) { REAL(access); outside("access"); return real(p, m); }
