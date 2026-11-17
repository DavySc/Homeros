/* file_save under crashes and outside writers, step by step.  The test
   defines the file-system calls file_save makes (the program's own
   definitions win over libc's), so it can log their order, kill the saving
   process before any one of them, or let another program write the file
   there.  Linux; the Windows twin is pending (phase 1).
   1. Order (Save2 "fsync", the power-loss half TLC checks): data is fsynced
      before a name points at it, the directory after; in place, the backup
      is durable before the file is touched, and removed only after.
   2. Crash before every step: the file is the old or the new text, whole;
      in place, a torn file only with the old text in the backup.
   3. An outside writer before every step (Save2 NoLossAtAll): its version is
      at the path or kept, and a version at the path is never overwritten by
      the next plain save.  Writers that replace by rename, write in place,
      or overwrite with the same size.  In place, the one window Save.tla
      says no check can close is measured, not hidden.
   4. The hard-link, read-only-directory and foreign-owner fallbacks keep the
      file's links, mode and owner and leave no backup.
   5. file_watch: outside changes seen, our own saves not. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "check.h"
#include "file.h"

/* ---- the interposed calls ---- */
enum { MAXLOG = 512 };
typedef struct { char op; int fd; } Op;
static Op logv[MAXLOG];
static int steps, crash_at, inject_at, quiet;
static void (*inject)(void);

/* ops: w write, s fsync, f fstat, t stat, l lstat, x renameat2, r rename,
   u unlink, c ftruncate, R read */
static void hook(char op, int fd)
{
	if (quiet) return;
	if (steps < MAXLOG) logv[steps] = (Op){ op, fd };
	steps++;
	if (steps == crash_at) _exit(77);
	if (steps == inject_at && inject) { quiet = 1; inject(); quiet = 0; }
}
ssize_t write(int fd, const void *b, size_t n) { hook('w', fd); return syscall(SYS_write, fd, b, n); }
ssize_t read(int fd, void *b, size_t n) { hook('R', fd); return syscall(SYS_read, fd, b, n); }
int fsync(int fd) { hook('s', fd); return (int)syscall(SYS_fsync, fd); }
int fstat(int fd, struct stat *st) { hook('f', fd); return (int)syscall(SYS_newfstatat, fd, "", st, AT_EMPTY_PATH); }
int stat(const char *restrict p, struct stat *restrict st) { hook('t', -1); return (int)syscall(SYS_newfstatat, AT_FDCWD, p, st, 0); }
int lstat(const char *restrict p, struct stat *restrict st) { hook('l', -1); return (int)syscall(SYS_newfstatat, AT_FDCWD, p, st, AT_SYMLINK_NOFOLLOW); }
int renameat2(int a, const char *p, int b, const char *q, unsigned fl) { hook('x', -1); return (int)syscall(SYS_renameat2, a, p, b, q, fl); }
int rename(const char *p, const char *q) { hook('r', -1); return (int)syscall(SYS_renameat2, AT_FDCWD, p, AT_FDCWD, q, 0); }
int unlink(const char *p) { hook('u', -1); return (int)syscall(SYS_unlinkat, AT_FDCWD, p, 0); }
int ftruncate(int fd, off_t n) { hook('c', fd); return (int)syscall(SYS_ftruncate, fd, n); }

/* ---- helpers ---- */
static char dir[] = "/tmp/homeros-save-XXXXXX", path[256], state[300], outside[64];
static const char OLD[] = "old text\nline two\n", NEW[] = "new text, longer than the old one\n";

static void put(const char *p, const char *s)
{
	quiet++;
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, s, strlen(s)) == (ssize_t)strlen(s));
	close(fd);
	quiet--;
}

static int is(const char *p, const char *s)
{
	char b[512];
	int fd = open(p, O_RDONLY);
	if (fd < 0) return s == NULL;
	quiet++;
	ssize_t r = read(fd, b, sizeof b);
	quiet--;
	close(fd);
	return s && r == (ssize_t)strlen(s) && !memcmp(b, s, (size_t)r);
}

static int load(FileId *id)
{
	char *d;
	size_t n;
	quiet++;
	int r = file_load(path, &d, &n, id);
	quiet--;
	free(d);
	return r;
}

static int save(Text *t, FileId *id, char *kept)
{
	kept[0] = 0;
	return file_save(path, t, text_len(t), id, 0, kept, 512);
}

/* outside writers; each writes a version nobody else has written */
static int nout;
static void by_rename(void)
{
	char tmp[300];
	snprintf(outside, sizeof outside, "outside %d, by rename\n", ++nout);
	snprintf(tmp, sizeof tmp, "%s/other", dir);
	put(tmp, outside);
	CHECK(!rename(tmp, path));
}
static void in_place(void)
{
	snprintf(outside, sizeof outside, "outside %d, in place\n", ++nout);
	put(path, outside);
}
static void same_size(void)
{
	/* the hardest for an identity check: same inode, same size */
	struct stat st;
	CHECK(!stat(path, &st));
	memset(outside, 'A' + (++nout % 26), (size_t)st.st_size);
	outside[st.st_size] = 0;
	int fd = open(path, O_WRONLY);
	CHECK(fd >= 0 && write(fd, outside, (size_t)st.st_size) == st.st_size);
	close(fd);
}

static int count_backups(const char *st)
{
	int k = 0;
	char cmd[400];
	snprintf(cmd, sizeof cmd, "ls %s/homeros/backup 2>/dev/null | wc -l", st);
	FILE *f = popen(cmd, "r");
	CHECK(f && fscanf(f, "%d", &k) == 1);
	pclose(f);
	return k;
}

static char want[512];   /* the text being saved */
static void wants(const Text *t) { want[text_read(t, 0, want, sizeof want - 1)] = 0; }

/* one dry run, logging the calls */
static int dry(int (*setup)(void), Text *t)
{
	FileId id;
	char kept[512];
	setup();
	CHECK(!load(&id));
	steps = 0; crash_at = inject_at = 0;
	wants(t);
	CHECK(save(t, &id, kept) == SAVE_OK && is(path, want));
	return steps;
}

static int first(char op, int fd, int from, int n)
{
	for (int i = from; i < n; i++) if (logv[i].op == op && (fd < 0 || logv[i].fd == fd)) return i;
	return -1;
}
static int last(char op, int fd, int n)   /* the last before index n */
{
	for (int i = n - 1; i >= 0; i--) if (logv[i].op == op && (fd < 0 || logv[i].fd == fd)) return i;
	return -1;
}

static char hard[300];
static int plain(void) { quiet++; unlink(hard); quiet--; put(path, OLD); return 0; }
static int linked(void) { plain(); return link(path, hard); }

/* 2: crash before step k, for every k */
static void crashes(int (*setup)(void), Text *t, int inplace)
{
	int n = dry(setup, t), kinds[2] = { 0 }, torn = 0;
	for (int k = 1; k <= n + 1; k++) {
		FileId id;
		char kept[512];
		setup();
		CHECK(!load(&id));
		fflush(stderr);
		pid_t p = fork();
		if (!p) { steps = 0; crash_at = k; inject_at = 0; _exit(save(t, &id, kept) == SAVE_OK ? 0 : 1); }
		int ws;
		CHECK(waitpid(p, &ws, 0) == p && WIFEXITED(ws));
		CHECK(WEXITSTATUS(ws) == 77 || (k == n + 1 && WEXITSTATUS(ws) == 0));
		int old = is(path, OLD), new = is(path, want);
		/* a torn file only in place, and only with the original in the backup */
		if (!old && !new) {
			char b[1024];
			CHECK(inplace);
			snprintf(b, sizeof b, "%s/homeros/backup/%s", state, path);
			for (char *c = b + strlen(state) + 16; *c; c++) if (*c == '/') *c = '%';
			CHECK(is(b, OLD));
			torn++;
		} else kinds[new]++;
		/* a crash leaves the temp or the backup behind; clear for the next run */
		char cmd[700];
		snprintf(cmd, sizeof cmd, "find %s -maxdepth 1 -name '.f.homeros-*' -delete; rm -rf %s/homeros/backup", dir, state);
		quiet++;
		CHECK(!system(cmd));
		quiet--;
	}
	CHECK(kinds[0] && kinds[1]);
	printf("  crash before each of %d steps%s: %d old, %d new, %d torn with the old text in the backup\n",
	       n + 1, inplace ? " (in place)" : "", kinds[0], kinds[1], torn);
}

/* 3: an outside writer before step k, for every k; returns the steps that lost it */
static int outsiders(int (*setup)(void), Text *t, void (*w)(void), int lost[])
{
	int n = dry(setup, t), nl = 0;
	for (int k = 1; k <= n + 1; k++) {
		FileId id;
		char kept[512];
		setup();
		CHECK(!load(&id));
		steps = 0; crash_at = 0; inject_at = k; inject = w;
		outside[0] = 0;
		int r = save(t, &id, kept);
		if (k == n + 1) { quiet = 1; w(); quiet = 0; }   /* after the save returned */
		inject_at = 0;
		CHECK(outside[0]);
		/* kept aside, or at the path and safe from the next plain save */
		int ok = r == SAVE_KEPT ? is(kept, outside) && is(path, want)
		       : is(path, outside) && r != SAVE_ERR && save(t, &id, kept) == SAVE_CHANGED && is(path, outside);
		if (!ok) lost[nl++] = k;
		if (r == SAVE_KEPT) unlink(kept);
	}
	return nl;
}

/* an outside writer before each step of file_load: either the buffer has
   its version, or the next plain save refuses (the identity is taken before
   the first read, so a write during the read is never folded into ours) */
static void loads(void)
{
	void (*writers[])(void) = { in_place, same_size };
	int n = 0;
	for (int i = 0; i < 2; i++)
		for (int k = 1;; k++) {
			FileId id;
			char *d, kept[512];
			size_t len;
			plain();
			steps = 0; inject_at = k; inject = writers[i]; outside[0] = 0;
			CHECK(!file_load(path, &d, &len, &id));
			inject_at = 0;
			if (!outside[0]) { free(d); break; }   /* past the last step */
			int has = len == strlen(outside) && !memcmp(d, outside, len);
			free(d);
			Text *t = text_new(NEW, strlen(NEW));
			CHECK(has || save(t, &id, kept) == SAVE_CHANGED);
			text_free(t);
			n++;
		}
	printf("  outside writer before each of %d load steps: in the buffer or refused at save\n", n);
}

static void order(void)
{
	Text *t = text_new(NEW, strlen(NEW));
	/* the swap: last write to the temp, its fsync, the swap, the directory */
	int n = dry(plain, t), tf = logv[first('w', -1, 0, n)].fd;
	int ws = last('w', tf, n), sy = first('s', tf, ws, n), x = first('x', -1, 0, n);
	CHECK(sy > ws && x > sy && first('s', -1, x, n) > x);
	/* in place: backup written and fsynced, then the file, its fsync, then the backup goes */
	n = dry(linked, t);
	int tgt = logv[first('f', -1, 0, n)].fd, bk = logv[first('w', -1, 0, n)].fd;
	CHECK(bk != tgt);
	int bs = first('s', bk, last('w', bk, n), n), w1 = first('w', tgt, 0, n);
	int ts = first('s', tgt, last('c', tgt, n), n), u = last('u', -1, n);
	CHECK(bs > last('w', bk, n) && w1 > bs && ts > w1 && u > ts);
	text_free(t);
	puts("  order: data durable before a name points at it; backup durable before, removed after");
}

static void fallbacks(void)
{
	Text *t = text_new(NEW, strlen(NEW));
	FileId id;
	char kept[512], *l = hard;
	struct stat st, sl;
	/* hard link: both names see the new text */
	linked();
	CHECK(!load(&id) && save(t, &id, kept) == SAVE_OK);
	CHECK(is(path, NEW) && is(l, NEW) && !stat(path, &st) && !stat(l, &sl) && st.st_ino == sl.st_ino && st.st_nlink == 2);
	CHECK(count_backups(state) == 0);
	/* shorter text: cut to size */
	Text *s = text_new("x\n", 2);
	CHECK(save(s, &id, kept) == SAVE_OK && is(path, "x\n") && is(l, "x\n"));
	text_free(s);
	plain();
	/* read-only directory and a file owned by someone else: as an
	   unprivileged user, since root may do both */
	char st2[300];
	snprintf(st2, sizeof st2, "%s/state2", dir);
	put(path, OLD);
	CHECK(!chmod(path, 0666) && !chown(path, 0, 0) && !mkdir(st2, 0700));
	CHECK(!chown(dir, 65534, 65534) && !chown(st2, 65534, 65534));
	fflush(stdout);
	pid_t p = fork();
	if (!p) {
		CHECK(!setgid(65534) && !setuid(65534) && !setenv("XDG_STATE_HOME", st2, 1));
		CHECK(!load(&id) && save(t, &id, kept) == SAVE_OK && is(path, NEW));
		CHECK(!stat(path, &st) && st.st_uid == 0 && (st.st_mode & 0777) == 0666);   /* owner kept */
		CHECK(!chmod(dir, 0555));
		Text *s2 = text_new(OLD, strlen(OLD));
		CHECK(!load(&id) && save(s2, &id, kept) == SAVE_OK && is(path, OLD));    /* read-only directory */
		CHECK(count_backups(st2) == 0);
		_exit(0);
	}
	int w;
	CHECK(waitpid(p, &w, 0) == p && WIFEXITED(w) && WEXITSTATUS(w) == 0);
	CHECK(!chmod(dir, 0700) && !chown(dir, 0, 0));
	text_free(t);
	puts("  fallbacks: hard link kept, read-only directory, foreign owner kept, no backup left");
}

/* a write that fails part way (a full disk, here a file-size limit): nothing
   replaced, the file as it was, nothing left behind */
static void errors(void)
{
	Text *t = text_new(NEW, strlen(NEW));
	fflush(stdout);
	pid_t p = fork();
	if (!p) {
		FileId id;
		char kept[512];
		struct rlimit rl = { strlen(OLD) + 4, strlen(OLD) + 4 };
		signal(SIGXFSZ, SIG_IGN);
		CHECK(!setrlimit(RLIMIT_FSIZE, &rl));
		plain();
		CHECK(!load(&id) && save(t, &id, kept) == SAVE_ERR && is(path, OLD));   /* the temp */
		linked();
		CHECK(!load(&id) && save(t, &id, kept) == SAVE_ERR && is(path, OLD) && !kept[0]);   /* in place */
		CHECK(count_backups(state) == 0);
		_exit(0);
	}
	int w;
	CHECK(waitpid(p, &w, 0) == p && WIFEXITED(w) && WEXITSTATUS(w) == 0);
	char cmd[400];
	snprintf(cmd, sizeof cmd, "test $(ls -A %s | grep -c homeros-) = 0", dir);
	CHECK(!system(cmd));
	plain();
	text_free(t);
	puts("  a write failing part way: nothing replaced, the original put back in place, nothing left behind");
}

static void watch(void)
{
	Text *t = text_new(NEW, strlen(NEW));
	FileId id;
	char kept[512], other[300];
	put(path, OLD);
	CHECK(!load(&id));
	int fd = file_watch(path);
	CHECK(fd >= 0);
	struct pollfd pf = { fd, POLLIN, 0 };
	CHECK(file_watch_read(fd, path, &id) == 0);
	CHECK(save(t, &id, kept) == SAVE_OK);                              /* ours */
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 0);
	snprintf(other, sizeof other, "%s/unrelated", dir);
	put(other, "x");                                                  /* not ours */
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 0);
	unlink(other);
	in_place();
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 1);
	CHECK(!load(&id) && file_watch_read(fd, path, &id) == 0);
	by_rename();
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 1);
	CHECK(!load(&id));
	same_size();
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 1);
	CHECK(!load(&id));
	unlink(path);
	CHECK(poll(&pf, 1, 1000) == 1 && file_watch_read(fd, path, &id) == 1);
	CHECK(poll(&pf, 1, 0) == 0);                                      /* drained */
	CHECK(file_watch(dir) == fd);                                     /* one instance */
	text_free(t);
	puts("  watch: outside rename, in-place, same-size writes and deletion seen; our save and other files not");
}

int main(void)
{
	CHECK(mkdtemp(dir));
	snprintf(path, sizeof path, "%s/f", dir);
	snprintf(state, sizeof state, "%s/state", dir);
	snprintf(hard, sizeof hard, "%s/hardlink", dir);
	CHECK(!mkdir(state, 0700) && !setenv("XDG_STATE_HOME", state, 1));
	Text *t = text_new(NEW, strlen(NEW));

	order();
	loads();
	crashes(plain, t, 0);
	crashes(linked, t, 1);
	Text *shorter = text_new("x\n", 2);   /* torn in place: new text, then the old one's tail */
	crashes(linked, shorter, 1);
	text_free(shorter);

	void (*writers[])(void) = { by_rename, in_place, same_size };
	const char *names[] = { "by rename", "in place", "same size" };
	int lost[MAXLOG];
	for (int i = 0; i < 3; i++) {
		int nl = outsiders(plain, t, writers[i], lost);
		CHECK(nl == 0);   /* Save2 NoLossAtAll, for every step */
		printf("  outside writer %s before each step: never lost\n", names[i]);
	}
	/* in place: losses only inside the window from the last identity check
	   to the identity taken after the write, which no check can close
	   (Save_ok_strong fails); a rename writer is never lost */
	CHECK(outsiders(linked, t, by_rename, lost) == 0);
	int n = dry(linked, t), tgt = logv[first('f', -1, 0, n)].fd;
	int check2 = last('f', tgt, first('w', tgt, 0, n));   /* the recheck, just before the first write */
	int fin = last('f', tgt, n);                          /* the identity taken after the write */
	for (int i = 1; i < 3; i++) {
		int nl = outsiders(linked, t, writers[i], lost);
		for (int k = 0; k < nl; k++) CHECK(lost[k] - 1 > check2 && lost[k] - 1 <= fin);
		printf("  in place, outside writer %s: lost at %d of %d steps, all inside the check-to-write window\n",
		       names[i], nl, n + 1);
	}
	fallbacks();
	errors();
	watch();
	text_free(t);
	quiet = 1;
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
	puts("test_file_save: ordered for power loss, whole after a crash at any step, outside writers never lost");
	return 0;
}
