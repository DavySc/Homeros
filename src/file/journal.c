/* The crash journal.  Layout (native byte order: it is read back on the
   machine that wrote it), every record closed by its own sum so a torn or
   unwritten tail is found and dropped:
     base: "homeros\1", u64 pathlen, path, u64 exists dev ino size,
           i64 mtime_ns, u32 state, u64 n, n bytes, u32 sum
     edit: u64 pos ndel nins, u32 state, nins bytes, u32 sum
   The base is the whole buffer, as Vim's swap file holds the whole buffer, so
   recovery needs nothing from the file, which may have changed meanwhile. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "file.h"
#include "state.h"

enum { SYNC_MS = 1000 };   /* DESIGN.md: fsync at most once per second */
static const char MAGIC[8] = { 'h', 'o', 'm', 'e', 'r', 'o', 's', 1 };

struct Journal {
	int fd;
	int based;          /* the base is written (since open or journal_saved) */
	int dirty;          /* appended since the last sync, from first_ms on */
	int dir_synced;     /* the journal's own directory entry is durable */
	long long first_ms;
	char name[PATH_MAX], file[PATH_MAX];
};

/* FNV-1a, 32 bits: finds a torn tail, not an attacker */
static uint32_t sum(uint32_t h, const void *p, size_t n)
{
	for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char *)p)[i]) * 16777619u;
	return h;
}
#define SUM0 2166136261u

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

typedef struct iovec Piece;   /* iov_base, iov_len */
#define PIECE(p, n) { (void *)(p), (n) }

static uint32_t sums(uint32_t h, const Piece *v, int k)
{
	for (int i = 0; i < k; i++) h = sum(h, v[i].iov_base, v[i].iov_len);
	return h;
}

/* Appends one record with one writev.  A failed write may leave part of it:
   nothing follows it, since the next change writes a new base from offset 0
   (journal_edit), and replay drops it should we crash first. */
static int append(Journal *j, Piece *v, int k)
{
	for (int i = 0; i < k;) {
		ssize_t w = writev(j->fd, v + i, k - i);
		if (w < 0 && errno == EINTR) continue;
		if (w < 0) return -1;
		for (; i < k && (size_t)w >= v[i].iov_len; i++) w -= (ssize_t)v[i].iov_len;
		if (i < k) { v[i].iov_base = (char *)v[i].iov_base + w; v[i].iov_len -= (size_t)w; }
	}
	if (!j->dirty) { j->dirty = 1; j->first_ms = now_ms(); }
	return 0;
}

typedef struct { const char *p; size_t n; int ok; } Rd;
static void take(Rd *r, void *out, size_t n)
{
	if (!r->ok || r->n < n) { r->ok = 0; return; }
	memcpy(out, r->p, n);
	r->p += n; r->n -= n;
}

/* Replays the journal in b into *t; *end is the length of the whole
   records that apply.  0 ok (*t NULL: no whole base), 1 the base names
   another file, -1 out of memory: the journal is then left untouched. */
static int replay(const char *b, size_t n, const char *file, Text **t, unsigned *state,
                  FileId *id, size_t *end)
{
	Rd r = { b, n, 1 };
	char m[8];
	uint64_t pl, f[4], len;
	int64_t mt;
	uint32_t st, s;
	*t = NULL;
	take(&r, m, 8);
	take(&r, &pl, 8);
	if (!r.ok || memcmp(m, MAGIC, 8) || pl > r.n) return 0;
	const char *p = r.p;
	r.p += pl; r.n -= (size_t)pl;
	take(&r, f, sizeof f);
	take(&r, &mt, 8);
	take(&r, &st, 4);
	take(&r, &len, 8);
	if (!r.ok || len > r.n) return 0;
	const char *base = r.p;
	r.p += len; r.n -= (size_t)len;
	take(&r, &s, 4);
	if (!r.ok || s != sum(SUM0, b, (size_t)(r.p - 4 - b))) return 0;
	if (pl != strlen(file) || memcmp(p, file, pl)) return 1;
	if (!(*t = text_new(base, (size_t)len))) return -1;
	*id = (FileId){ (int)f[0], f[1], f[2], f[3], mt };
	*state = st;
	*end = (size_t)(r.p - b);
	for (;;) {
		const char *at = r.p;
		uint64_t e[3];
		take(&r, e, sizeof e);
		take(&r, &st, 4);
		if (!r.ok || e[2] > r.n) return 0;
		const char *ins = r.p;
		r.p += e[2]; r.n -= (size_t)e[2];
		take(&r, &s, 4);
		if (!r.ok || s != sum(SUM0, at, (size_t)(r.p - 4 - at))) return 0;
		/* a whole record that does not apply ends the journal like a torn
		   one: only a bug writes it, and refusing it would keep the file
		   without a journal for good */
		if (e[0] > text_len(*t) || e[1] > text_len(*t) - e[0]) return 0;
		if (text_delete(*t, (size_t)e[0], (size_t)e[1]) || text_insert(*t, (size_t)e[0], ins, (size_t)e[2])) {
			text_free(*t);
			*t = NULL;
			return -1;
		}
		*state = st;
		*end = (size_t)(r.p - b);
	}
}

int journal_open(const char *path, Journal **jp, Text **t, unsigned *state, FileId *id)
{
	Journal *j = calloc(1, sizeof *j);
	long long size;
	int l;
	*jp = NULL; *t = NULL;
	if (!j || state_abs(path, j->file)) goto fail;
	for (;;) {
		j->fd = state_open("journal", j->file, j->name, sizeof j->name, O_RDWR | O_CREAT | O_APPEND);
		if (j->fd < 0) goto fail;
		if (!(l = state_lock(j->fd, j->name, &size))) break;
		if (l < 0) goto fail_fd;
		close(j->fd);
	}
	/* just opened, so the read starts at 0 */
	char *buf = malloc((size_t)size + 1);
	ssize_t got = buf ? read(j->fd, buf, (size_t)size) : -1;
	if (got < 0) { free(buf); goto fail_fd; }
	size_t end = 0;
	int r = replay(buf, (size_t)got, j->file, t, state, id, &end);
	free(buf);
	/* two paths can share a name ('%' in a path); leave the other's alone */
	if (r) { errno = r > 0 ? EEXIST : ENOMEM; goto fail_fd; }
	if (ftruncate(j->fd, (off_t)end)) {}   /* drop a torn tail before appending */
	j->based = *t != NULL;
	*jp = j;
	return *t != NULL;
fail_fd:
	{ int e = errno; close(j->fd); errno = e; }
fail:
	{ int e = errno; free(j); errno = e; }
	return -1;
}

int journal_base(Journal *j, const Text *t, unsigned state, const FileId *id)
{
	if (j->based) return 0;
	uint64_t pl = strlen(j->file), f[4] = { (uint64_t)id->exists, id->dev, id->ino, id->size };
	int64_t mt = id->mtime_ns;
	uint32_t st = state;
	uint64_t n = text_len(t);
	Piece head[] = { PIECE(MAGIC, 8), PIECE(&pl, 8), PIECE(j->file, pl), PIECE(f, sizeof f),
	                 PIECE(&mt, 8), PIECE(&st, 4), PIECE(&n, 8) };
	uint32_t h = sums(SUM0, head, 7);
	char b[1 << 16];
	if (ftruncate(j->fd, 0)) return -1;
	if (append(j, head, 7)) goto fail;
	for (size_t pos = 0; pos < n;) {
		size_t k = text_read(t, pos, b, n - pos < sizeof b ? n - pos : sizeof b);
		Piece v = PIECE(b, k);
		h = sums(h, &v, 1);
		if (append(j, &v, 1)) goto fail;
		pos += k;
	}
	Piece v = PIECE(&h, 4);
	if (append(j, &v, 1)) goto fail;
	j->based = 1;
	return 0;
fail:
	/* a torn base is no base; the core has said E297 (the text is the
	   file's, unless this base follows a failed append) */
	if (ftruncate(j->fd, 0)) {}
	return -1;
}

int journal_edit(Journal *j, size_t pos, size_t ndel, const char *ins, size_t nins, unsigned state)
{
	uint64_t e[3] = { pos, ndel, nins };
	uint32_t st = state, h;
	/* no base (or a hole since it): a record now would replay onto the
	   wrong text, so it waits for journal_base */
	if (!j->based) { errno = EINVAL; return -1; }
	Piece v[] = { PIECE(e, sizeof e), PIECE(&st, 4), PIECE(ins, nins), PIECE(&h, 4) };
	h = sums(SUM0, v, 3);
	if (!append(j, v, 4)) return 0;
	/* the change is not in the journal, so later ones would replay onto the
	   wrong text: the next change writes a new base instead */
	j->based = 0;
	return -1;
}

void journal_saved(Journal *j)
{
	/* not synced: should a crash bring the old journal back, it recovers
	   exactly the text just saved, which the core sees equals the file */
	if (ftruncate(j->fd, 0)) {}
	j->based = 0;
}

int journal_due(const Journal *j)
{
	if (!j->dirty) return -1;
	long long d = j->first_ms + SYNC_MS - now_ms();
	return d < 0 ? 0 : (int)d;
}

void journal_sync(Journal *j)
{
	if (!j->dirty) return;
	fdatasync(j->fd);
	/* the name, once, or a power loss can lose the whole file */
	if (!j->dir_synced) j->dir_synced = !state_sync_dir(j->name);
	j->dirty = 0;
}

void journal_close(Journal *j)
{
	if (!j) return;
	state_close(j->fd, j->name);   /* removes the name before closing: the lock covers the removal */
	free(j);
}
