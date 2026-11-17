/* The crash journal against a model: the buffer as a plain array, edited at
   random, saved now and then.
   1. Killed at a random moment (SIGKILL, 300 sessions): the next open gives
      back exactly the text, flags and file identity of the last edit that
      returned, or of the one being written; nothing after a save but no
      edit.
   2. Cut at every byte of its tail: the whole records come back, the torn
      one is dropped, and an edit appended after it is found again.
   3. A changed byte in any record, or garbage after the end: that record and
      everything after it are dropped, nothing before.
   4. A second editor on the same file (also by a symlink) is kept out while
      the first lives, and recovers once it is killed.
   5. A clean close leaves nothing; a new file and a name shared by two paths
      ('%' in a path) are handled.
   6. Durable within a second: journal_due counts down from 1000 ms, and
      journal_sync calls fdatasync, and fsyncs the directory once. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
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

static int datasyncs, fsyncs;
int fdatasync(int fd) { datasyncs++; return (int)syscall(SYS_fdatasync, fd); }
int fsync(int fd) { fsyncs++; return (int)syscall(SYS_fsync, fd); }

static char dir[] = "/tmp/homeros-journal-XXXXXX", path[256], jname[700];

/* the model */
enum { CAP = 1 << 16 };
typedef struct { char b[CAP]; size_t n; unsigned state; FileId id; } Model;

static int equal(const Text *t, const Model *m)
{
	static char b[CAP];
	return t && text_len(t) == m->n && text_read(t, 0, b, CAP) == m->n && !memcmp(b, m->b, m->n);
}

static int same_id(const FileId *a, const FileId *b)
{
	return a->exists == b->exists && a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_ns == b->mtime_ns;
}

/* one random step on both the journal and the model; 1 if it was a save */
static int step(Journal *j, Text **t, Model *m, int saves)
{
	if (saves && rnd(20) == 0) {
		journal_saved(j);
		m->id = (FileId){ 1, rnd(9), rnd(1000), m->n, (long long)rnd(1u << 30) };
		return 1;
	}
	CHECK(!journal_base(j, *t, m->state, &m->id));
	size_t pos = rnd((unsigned)m->n + 1), ndel = rnd((unsigned)(m->n - pos) + 1) % 8, nins = rnd(6);
	char ins[8];
	for (size_t i = 0; i < nins; i++) ins[i] = "ab\n\xc3\xa9\0x"[rnd(7)];
	if (m->n - ndel + nins >= CAP) ndel = m->n - pos, nins = 0;
	CHECK(!text_delete(*t, pos, ndel) && !text_insert(*t, pos, ins, nins));
	memmove(m->b + pos + nins, m->b + pos + ndel, m->n - pos - ndel);
	memcpy(m->b + pos, ins, nins);
	m->n = m->n - ndel + nins;
	m->state = rnd(4);
	CHECK(!journal_edit(j, pos, ndel, ins, nins, m->state));
	return 0;
}

static void start(Model *m, const char *s)
{
	memset(m, 0, sizeof *m);
	m->n = strlen(s);
	memcpy(m->b, s, m->n);
	m->id = (FileId){ 1, 3, 77, m->n, 123456789 };
}

/* the model after k steps from seed; 1 if nothing is left to recover
   (no step yet, or the last one was a save) */
static int simulate(unsigned long long seed, unsigned k, Model *m)
{
	char other[300];
	Journal *j;
	Text *t = text_new("first line\n", 11), *x;
	unsigned st;
	FileId id;
	unsigned long long keep = rng;
	int saved = 1;
	rng = seed;
	start(m, "first line\n");
	snprintf(other, sizeof other, "%s/replay", dir);   /* its own journal */
	CHECK(journal_open(other, &j, &x, &st, &id) == 0);
	for (unsigned i = 0; i < k; i++) saved = step(j, &t, m, 1);
	journal_close(j);
	text_free(t);
	rng = keep;
	return saved;
}

static void killed(void)
{
	int nothing = 0, mid = 0;
	for (int s = 0; s < 300; s++) {
		unsigned long long seed = rng ^ (unsigned long long)s * 0x9E3779B97F4A7C15ULL;
		int fds[2];
		CHECK(!pipe(fds));
		fflush(stdout);
		pid_t p = fork();
		if (!p) {
			Model m;
			Journal *j;
			Text *t = text_new("first line\n", 11), *x;
			unsigned st;
			FileId id;
			rng = seed;
			start(&m, "first line\n");
			CHECK(journal_open(path, &j, &x, &st, &id) == 0);
			for (unsigned k = 1;; k++) {
				step(j, &t, &m, 1);
				CHECK(write(fds[1], &k, sizeof k) == sizeof k);
			}
		}
		close(fds[1]);
		unsigned k = 0, got;
		for (int stop = 1 + (int)rnd(300); stop-- && read(fds[0], &got, sizeof got) == sizeof got;) k = got;
		kill(p, SIGKILL);
		CHECK(waitpid(p, NULL, 0) == p);
		while (read(fds[0], &got, sizeof got) == sizeof got) k = got;   /* acks still in the pipe */
		close(fds[0]);
		/* step k returned; step k + 1 may have been written, not acknowledged */
		static Model m, next;
		int done = simulate(seed, k, &m), more = simulate(seed, k + 1, &next);
		Journal *j;
		Text *t;
		unsigned st;
		FileId id;
		int r = journal_open(path, &j, &t, &st, &id);
		CHECK(r >= 0);
		if (r == 0) { CHECK(done || more); nothing++; }
		/* after a save, killed between the next edit's base and its record:
		   the base alone, which is the saved text */
		else if (equal(t, &m) && st == m.state && same_id(&id, &m.id)) {}
		else { CHECK(!more && equal(t, &next) && st == next.state && same_id(&id, &next.id)); mid++; }
		text_free(t);
		journal_close(j);
	}
	printf("  killed 300 times: each recovered the last edit that returned (%d the one being written, %d had nothing left after a save)\n", mid, nothing);
}

/* a session in a child that then dies: base, then n records; snap[i] is
   the model after i records and end[i] the journal's size then */
enum { NREC = 12 };
static Model snap[NREC + 1];
static off_t end[NREC + 1];
static void session(void)
{
	fflush(stdout);
	pid_t p = fork();
	if (!p) {
		Journal *j;
		Text *t = text_new("base line\n", 10), *x;
		unsigned st;
		FileId id;
		struct stat sb;
		CHECK(journal_open(path, &j, &x, &st, &id) == 0);
		CHECK(!journal_base(j, t, snap[0].state, &snap[0].id));
		CHECK(!stat(jname, &sb));
		end[0] = sb.st_size;
		for (int i = 1; i <= NREC; i++) {
			snap[i] = snap[i - 1];
			step(j, &t, &snap[i], 0);
			CHECK(!stat(jname, &sb));
			end[i] = sb.st_size;
		}
		FILE *f = fopen("session", "wb");   /* hand the snapshots to the parent */
		CHECK(f && fwrite(snap, sizeof snap, 1, f) == 1 && fwrite(end, sizeof end, 1, f) == 1);
		fclose(f);
		_exit(0);
	}
	int w;
	CHECK(waitpid(p, &w, 0) == p && WIFEXITED(w) && !WEXITSTATUS(w));
	FILE *f = fopen("session", "rb");
	CHECK(f && fread(snap, sizeof snap, 1, f) == 1 && fread(end, sizeof end, 1, f) == 1);
	fclose(f);
	unlink("session");
}

static char *slurp(size_t *n)
{
	static char b[1 << 16];
	int fd = open(jname, O_RDONLY);
	CHECK(fd >= 0);
	ssize_t r = read(fd, b, sizeof b);
	CHECK(r > 0);
	close(fd);
	*n = (size_t)r;
	return b;
}

static void put(const char *b, size_t n)
{
	int fd = open(jname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0 && write(fd, b, n) == (ssize_t)n);
	close(fd);
}

/* which records are whole in the first n bytes */
static int whole(size_t n)
{
	int i = -1;
	while (i < NREC && (size_t)end[i + 1] <= n) i++;
	return i;   /* -1: not even the base */
}

/* the journal as it is now must recover snap[i] (i < 0: nothing); then an
   edit appended by a process that dies must come back too */
static void recovers(int i)
{
	fflush(stdout);
	pid_t p = fork();
	if (!p) {
		Journal *j;
		Text *t;
		unsigned st;
		FileId id;
		int r = journal_open(path, &j, &t, &st, &id);
		CHECK(r == (i >= 0));
		if (r) {
			CHECK(equal(t, &snap[i]) && st == snap[i].state && same_id(&id, &snap[i].id));
			CHECK(!journal_base(j, t, 9, &id) && !journal_edit(j, 0, 0, "Z", 1, 5));
		}
		_exit(0);
	}
	int w;
	CHECK(waitpid(p, &w, 0) == p && WIFEXITED(w) && !WEXITSTATUS(w));
	if (i < 0) return;
	Model m = snap[i];
	memmove(m.b + 1, m.b, m.n++);
	m.b[0] = 'Z';
	m.state = 5;
	Journal *j;
	Text *t;
	unsigned st;
	FileId id;
	CHECK(journal_open(path, &j, &t, &st, &id) == 1 && equal(t, &m) && st == 5 && same_id(&id, &m.id));
	text_free(t);
	journal_close(j);
}

static void torn(void)
{
	start(&snap[0], "base line\n");
	snap[0].state = 1;
	session();
	size_t n;
	char *b = slurp(&n), copy[1 << 16];
	CHECK(n == (size_t)end[NREC]);
	memcpy(copy, b, n);
	/* 2: every cut from just before the base's end to the end */
	for (size_t cut = (size_t)end[0] - 1; cut <= n; cut++) { put(copy, cut); recovers(whole(cut)); }
	/* 3: one changed byte anywhere drops its record and all after it */
	for (size_t at = 0; at < n; at++) {
		copy[at] ^= 0x20;
		put(copy, n);
		recovers(whole(at));
		copy[at] ^= 0x20;
	}
	/* and garbage after the end changes nothing */
	for (int g = 0; g < 50; g++) {
		size_t k = 1 + rnd(40);
		for (size_t i = 0; i < k; i++) copy[n + i] = (char)rnd(256);
		put(copy, n + k);
		recovers(NREC);
	}
	printf("  cut at each of %zu bytes and changed at each of %zu: whole records kept, the rest dropped, appends found again\n",
	       n - (size_t)end[0] + 2, n);
}

static void locks(void)
{
	char link[300];
	int fds[2];
	snprintf(link, sizeof link, "%s/link", dir);
	CHECK(!symlink(path, link) && !pipe(fds));
	fflush(stdout);
	pid_t p = fork();
	if (!p) {
		Journal *j;
		Text *t = text_new("x\n", 2), *x;
		unsigned st;
		FileId id = { 0 };
		CHECK(journal_open(link, &j, &x, &st, &id) == 0);
		CHECK(!journal_base(j, t, 0, &id) && !journal_edit(j, 0, 0, "y", 1, 0));
		char c = 1;
		CHECK(write(fds[1], &c, 1) == 1);
		pause();
	}
	char c;
	CHECK(read(fds[0], &c, 1) == 1);
	Journal *j;
	Text *t;
	unsigned st;
	FileId id;
	errno = 0;
	CHECK(journal_open(path, &j, &t, &st, &id) == -1 && errno == EWOULDBLOCK);
	kill(p, SIGKILL);
	CHECK(waitpid(p, NULL, 0) == p);
	CHECK(journal_open(path, &j, &t, &st, &id) == 1 && text_len(t) == 3);
	text_free(t);
	/* 5: a clean close leaves nothing */
	journal_close(j);
	CHECK(access(jname, F_OK) == -1 && errno == ENOENT);
	CHECK(journal_open(path, &j, &t, &st, &id) == 0 && !t);
	journal_close(j);
	close(fds[0]); close(fds[1]);
	unlink(link);
	puts("  a second editor (by a symlink too) kept out while the first lives, recovers once it is killed");
}

static void names(void)
{
	char a[300], b[300], sub[300];
	Journal *j;
	Text *t, *x = text_new("", 0);
	unsigned st;
	FileId id = { 0 };
	/* a new file */
	snprintf(a, sizeof a, "%s/new.txt", dir);
	CHECK(journal_open(a, &j, &t, &st, &id) == 0);
	CHECK(!journal_base(j, x, 0, &id) && !journal_edit(j, 0, 0, "n", 1, 0));
	fflush(stdout);
	/* two paths, one journal name: the second leaves the first's alone */
	snprintf(a, sizeof a, "%s/a%%b", dir);
	snprintf(sub, sizeof sub, "%s/a", dir);
	snprintf(b, sizeof b, "%s/a/b", dir);
	CHECK(!mkdir(sub, 0700));
	pid_t p = fork();
	if (!p) {
		Journal *k;
		CHECK(journal_open(a, &k, &t, &st, &id) == 0);
		CHECK(!journal_base(k, x, 0, &id) && !journal_edit(k, 0, 0, "a", 1, 0));
		_exit(0);
	}
	CHECK(waitpid(p, NULL, 0) == p);
	Journal *k;
	errno = 0;
	CHECK(journal_open(b, &k, &t, &st, &id) == -1 && errno == EEXIST);
	CHECK(journal_open(a, &k, &t, &st, &id) == 1 && text_len(t) == 1);
	text_free(t);
	journal_close(k);
	journal_close(j);
	text_free(x);
	puts("  a new file journals; two paths sharing a journal name do not mix");
}

static void durable(void)
{
	Journal *j;
	Text *t = text_new("d\n", 2), *x;
	unsigned st;
	FileId id = { 0 };
	CHECK(journal_open(path, &j, &x, &st, &id) == 0);
	CHECK(journal_due(j) == -1);
	CHECK(!journal_base(j, t, 0, &id) && !journal_edit(j, 0, 0, "e", 1, 0));
	struct stat sb;   /* unsaved text: for its owner only */
	CHECK(!stat(jname, &sb) && (sb.st_mode & 0777) == 0600);
	char jd[700];
	snprintf(jd, sizeof jd, "%s", jname);
	*strrchr(jd, '/') = 0;
	CHECK(!stat(jd, &sb) && (sb.st_mode & 0777) == 0700);
	int d = journal_due(j);
	CHECK(d > 900 && d <= 1000);
	usleep(200000);
	d = journal_due(j);
	CHECK(d > 700 && d <= 800);   /* counted from the first unsynced record, not the last */
	CHECK(!journal_edit(j, 0, 0, "f", 1, 0) && journal_due(j) <= d);
	datasyncs = fsyncs = 0;
	journal_sync(j);
	CHECK(datasyncs == 1 && fsyncs == 1 && journal_due(j) == -1);
	CHECK(!journal_edit(j, 0, 0, "g", 1, 0));
	journal_sync(j);
	CHECK(datasyncs == 2 && fsyncs == 1);   /* the directory once */
	journal_sync(j);
	CHECK(datasyncs == 2);                  /* nothing pending, nothing done */
	/* after a failed append (a full disk; here a size limit) the next change
	   writes a new base, as the core calls it (a change journalled
	   after the hole would replay onto the wrong text: review of PR 16) */
	CHECK(!text_insert(t, 0, "gfe", 3));
	struct rlimit rl, low;
	CHECK(!getrlimit(RLIMIT_FSIZE, &rl) && !stat(jname, &sb));
	low = (struct rlimit){ (rlim_t)sb.st_size + 40, rl.rlim_max };
	signal(SIGXFSZ, SIG_IGN);
	CHECK(!setrlimit(RLIMIT_FSIZE, &low));
	char big[100] = { 0 };
	CHECK(journal_edit(j, 0, 0, big, sizeof big, 0) == -1);
	CHECK(!setrlimit(RLIMIT_FSIZE, &rl));
	CHECK(!text_insert(t, 0, big, sizeof big));
	CHECK(journal_edit(j, 0, 0, "h", 1, 7) == -1);   /* not before the new base */
	CHECK(!journal_base(j, t, 0, &id) && !journal_edit(j, 0, 0, "h", 1, 7) && !text_insert(t, 0, "h", 1));
	/* a crash: keep the bytes, not the clean close's removal */
	int fd = open(jname, O_RDONLY);
	char copy[4096];
	ssize_t n = read(fd, copy, sizeof copy);
	close(fd);
	journal_close(j);
	fd = open(jname, O_WRONLY | O_CREAT, 0600);
	CHECK(n > 0 && write(fd, copy, (size_t)n) == n);
	close(fd);
	char a[256], b[256];
	CHECK(journal_open(path, &j, &x, &st, &id) == 1 && st == 7 && text_len(x) == text_len(t)
	      && text_read(x, 0, a, sizeof a) == text_read(t, 0, b, sizeof b) && !memcmp(a, b, text_len(t)));
	text_free(x);
	/* a whole record that does not apply (only a bug writes one) ends the
	   journal like a torn tail, rather than keeping it from opening */
	CHECK(!journal_edit(j, 999, 0, "x", 1, 3));
	fd = open(jname, O_RDONLY);
	n = read(fd, copy, sizeof copy);
	close(fd);
	journal_close(j);
	fd = open(jname, O_WRONLY | O_CREAT, 0600);
	CHECK(n > 0 && write(fd, copy, (size_t)n) == n);
	close(fd);
	CHECK(journal_open(path, &j, &x, &st, &id) == 1 && st == 7 && text_len(x) == text_len(t));
	text_free(x);
	journal_close(j);
	text_free(t);
	puts("  durable within a second: due counts down from the first unsynced record; sync is fdatasync, the directory once; owner only; a failed append followed by a new base; a record that does not apply ends the journal");
}

int main(void)
{
	CHECK(mkdtemp(dir));
	snprintf(path, sizeof path, "%s/f.txt", dir);
	char st[300];
	snprintf(st, sizeof st, "%s/state", dir);
	CHECK(!mkdir(st, 0700) && !setenv("XDG_STATE_HOME", st, 1));
	snprintf(jname, sizeof jname, "%s/homeros/journal/%s", st, path);
	for (char *c = jname + strlen(st) + 17; *c; c++) if (*c == '/') *c = '%';
	int fd = open(path, O_WRONLY | O_CREAT, 0644);
	close(fd);

	CHECK(!chdir(dir));
	killed();
	torn();
	locks();
	names();
	durable();
	puts("test_file_journal: recovered after kill -9, torn and corrupt tails dropped, one editor per file");
	return 0;
}
