/* The undo tree: Neovim 0.12.6's undo.c ported, with byte edits in place of
   saved lines.  The header links, u_savecommon()'s size cap, u_doit(),
   undo_time(), u_freeheader(), u_freebranch() and u_update_save_nr() are
   copied rule for rule (each function names its original; see Head for the
   branches left out), because only the differential test against nvim pins
   the last-visited rule (DESIGN.md, Formal models) and the cap's choice of
   what to drop.  A group stores its
   edits from the parent state, so undo applies their inverses in reverse
   (Undo.lean, undo_group) and a jump walks up, then down (Undo.lean, jump). */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "undo.h"

#ifndef UNDO_LEVELS
#define UNDO_LEVELS 1000   /* 'undolevels' (FEATURES.md); the differential test lowers it */
#endif

typedef struct { size_t pos, ndel, nins; char *b; } Edit;   /* b: deleted, then inserted bytes */
typedef struct Head Head;
/* u_header_T: next is the older change (the parent), prev the newer one
   that redo takes, alt_next and alt_prev the other changes made from the
   same state, last visited first.  Neovim keeps three facts that its code
   does not rely on and ours does, so its branches for the other case are
   left out: a parent's prev, curhead and oldhead each have no alt_prev, and
   oldhead has no next.  Measured, not proved: under gcov, 4,000 differential
   sequences (undolevels 1000 and 3) and both unit tests never took Neovim's
   branches for the other case; undo_read() refuses a tree without these
   facts, and the fuzzer reads back every tree it builds. */
struct Head {
	Head *next, *prev, *alt_next, *alt_prev;
	Edit *e;
	size_t n, cap;
	UndoCursor cur;
	unsigned after;
	long seq, save, walk;
	long long time;
};
struct Undo {
	Head *newhead, *curhead, *oldhead;
	long numhead, seq_last, seq_cur, save_last, save_cur, lastmark;
	long long time_cur;
	int open;
	int phase;                 /* the planned move: 0 none, 1 going up, 2 going down */
	UndoHook *hook;
	void *ctx;
	long long target;
	long mark, nomark;
	int above;
};

Undo *undo_new(void) { return calloc(1, sizeof(Undo)); }

/* u_freeentries() */
static void freeentries(Undo *u, Head *h, Head **hp)
{
	if (u->curhead == h) u->curhead = NULL;
	if (u->newhead == h) u->newhead = NULL;
	if (hp && *hp == h) *hp = NULL;
	for (size_t i = 0; i < h->n; i++) free(h->e[i].b);
	free(h->e);
	free(h);
	u->numhead--;
}

static void freebranch(Undo *u, Head *h, Head **hp);

/* u_freeheader(), which is only called on oldhead: its alternatives can
   never be reached again, its children become the oldest changes */
static void freeoldest(Undo *u, Head **hp)
{
	Head *h = u->oldhead;
	if (h->alt_next) freebranch(u, h->alt_next, hp);
	u->oldhead = h->prev;
	if (!h->prev) u->newhead = NULL;
	for (Head *a = h->prev; a; a = a->alt_next) a->next = NULL;
	freeentries(u, h, hp);
}

/* u_freebranch(): h, everything below it and the alternatives after it */
static void freebranch(Undo *u, Head *h, Head **hp)
{
	if (h == u->oldhead) {
		while (u->oldhead) freeoldest(u, hp);
		return;
	}
	if (h->alt_prev) h->alt_prev->alt_next = NULL;
	while (h) {
		Head *f = h;
		if (f->alt_next) freebranch(u, f->alt_next, hp);
		h = f->prev;
		freeentries(u, f, hp);
	}
}

void undo_free(Undo *u)
{
	if (!u) return;
	while (u->oldhead) freeoldest(u, NULL);
	free(u);
}

/* u_savecommon(), the part that opens a group */
static int open_group(Undo *u, UndoCursor cur)
{
	Head *h = calloc(1, sizeof *h);
	if (!h) return -1;
	/* undid more than redid: the redo part becomes an alternative branch */
	Head *old = u->curhead;
	if (old) {
		u->newhead = old->next;
		u->curhead = NULL;
	}
	while (u->numhead > UNDO_LEVELS && u->oldhead) {
		Head *f = u->oldhead;
		if (f == old) freebranch(u, f, &old);   /* can't reconnect it */
		else if (!f->alt_next) freeoldest(u, &old);
		else {                                  /* the oldest alternative, whole */
			while (f->alt_next) f = f->alt_next;
			freebranch(u, f, &old);
		}
	}
	h->next = u->newhead;
	h->alt_next = old;
	if (old) {
		old->alt_prev = h;
		if (u->oldhead == old) u->oldhead = h;
	}
	if (u->newhead) u->newhead->prev = h;
	h->seq = u->seq_cur = ++u->seq_last;
	h->time = time(NULL);
	u->time_cur = h->time + 1;
	h->cur = cur;
	u->newhead = h;
	if (!u->oldhead) u->oldhead = h;
	u->numhead++;
	u->open = 1;
	return 0;
}

int undo_record(Undo *u, size_t pos, const char *del, size_t ndel,
                const char *ins, size_t nins, UndoCursor cur, unsigned after)
{
	u->phase = 0;
	if (!u->open && open_group(u, cur)) return -1;
	Head *g = u->newhead;
	g->after = after;
	if (g->n == g->cap) {
		size_t cap = g->cap ? 2 * g->cap : 4;
		Edit *e = realloc(g->e, cap * sizeof *e);
		if (!e) return -1;
		g->e = e;
		g->cap = cap;
	}
	char *b = malloc(ndel + nins ? ndel + nins : 1);
	if (!b) return -1;
	if (ndel) memcpy(b, del, ndel);
	if (nins) memcpy(b + ndel, ins, nins);
	g->e[g->n++] = (Edit){ pos, ndel, nins, b };
	return 0;
}

void undo_close(Undo *u) { u->open = 0; }

void undo_hook(Undo *u, UndoHook *fn, void *ctx)
{
	u->hook = fn;
	u->ctx = ctx;
}

/* The changed span of the result: bytes before the lowest edit and after the
   shortest untouched tail are the same before and after the group. */
static void span(const Head *g, size_t len, int undo, UndoWhere *w)
{
	size_t lo = (size_t)-1, tail = (size_t)-1, l = len;
	if (undo) /* replay lengths forward from the result (the old text) */
		for (size_t i = 0; i < g->n; i++) {
			const Edit *e = &g->e[i];
			l = l - e->ndel + e->nins;
			if (e->pos < lo) lo = e->pos;
			if (l - e->pos - e->nins < tail) tail = l - e->pos - e->nins;
		}
	else
		for (size_t i = g->n; i-- > 0;) {
			const Edit *e = &g->e[i];
			if (e->pos < lo) lo = e->pos;
			if (l - e->pos - e->nins < tail) tail = l - e->pos - e->nins;
			l = l - e->nins + e->ndel;
		}
	if (!g->n) lo = 0, tail = len;   /* a group whose first edit ran out of memory */
	w->lo = lo;
	w->hi = len - tail;
	w->cur = g->cur;
	w->flags = undo ? g->cur.flags : g->after;
}

/* u_undoredo(): the text part, then where we are for g-, :earlier Nf and
   :earlier Ns */
static int undoredo(Undo *u, Text *t, int undo, UndoWhere *w)
{
	Head *h = u->curhead;
	for (size_t k = 0; k < h->n; k++) {
		Edit *e = &h->e[undo ? h->n - 1 - k : k];
		size_t nd = undo ? e->nins : e->ndel, ni = undo ? e->ndel : e->nins;
		const char *ins = undo ? e->b : e->b + e->ndel;
		if (u->hook) u->hook(u->ctx, t, e->pos, nd, ins, ni);
		if (text_delete(t, e->pos, nd) || text_insert(t, e->pos, ins, ni)) return -1;
	}
	span(h, text_len(t), undo, w);
	/* after an undo we are just above the undone change, so that
	   ":earlier 1s" works (Neovim's comment) */
	u->seq_cur = undo ? (h->next ? h->next->seq : 0) : h->seq;
	if (h->save) u->save_cur = undo ? h->save - 1 : h->save;
	u->time_cur = h->time;
	return 0;
}

/* u_doit(), undoing */
int undo_undo(Undo *u, Text *t, UndoWhere *w)
{
	u->open = 0;
	u->phase = 0;
	Head *h = u->curhead ? u->curhead->next : u->newhead;
	if (!u->numhead || !h) {
		u->curhead = u->oldhead;   /* stick curhead at the end */
		return 0;
	}
	u->curhead = h;
	return undoredo(u, t, 1, w) ? -1 : 1;
}

/* u_doit(), redoing */
int undo_redo(Undo *u, Text *t, UndoWhere *w)
{
	u->open = 0;
	u->phase = 0;
	Head *h = u->curhead;
	if (!h) return 0;
	if (undoredo(u, t, 0, w)) return -1;
	if (!h->prev) u->newhead = h;
	u->curhead = h->prev;
	return 1;
}

unsigned long undo_seq(const Undo *u) { return (unsigned long)u->seq_cur; }

static long long dist(long long a, long long b) { return a > b ? a - b : b - a; }

/* undo_time(): the search.  "target" is the state below which we want to be;
   the walk marks with "mark" the headers on the way to it. */
int undo_time(Undo *u, long step, int unit)
{
	int sec = unit == UNDO_SEC, file = unit == UNDO_FILE, absolute = unit == UNDO_ABS, above = 0;
	long long target, closest;
	Head *h;
	u->open = 0;
	u->phase = 0;
	if (absolute) {
		target = step;
		closest = -1;
	} else {
		if (sec) target = u->time_cur + step;
		else if (file) {
			if (step < 0) {
				/* changes since the last write count as one write, so that
				   ":earlier 1f" undoes them */
				h = u->curhead ? u->curhead->next : u->newhead;
				target = u->save_cur + step + (h && h->save ? 0 : 1);
				if (target <= 0) file = 0;      /* before the oldest change */
			} else {
				target = u->save_cur + step;
				if (target > u->save_last) {    /* after the newest change */
					target = u->seq_last + 1;
					file = 0;
				}
			}
		} else target = u->seq_cur + step;
		if (step < 0) {
			if (target < 0) target = 0;
			closest = -1;
		} else {
			closest = sec ? (long long)time(NULL) + 1 : file ? u->save_last + 2 : u->seq_last + 2;
			if (target >= closest) target = closest - 1;
		}
	}
	long long closest_start = closest, closest_seq = u->seq_cur;
	if (target) {
		/* round 1 looks for target and the closest match; round 2 for that */
		for (int round = 1; round <= 2; round++) {
			u->mark = ++u->lastmark;
			u->nomark = ++u->lastmark;
			for (h = u->curhead ? u->curhead : u->newhead; h;) {
				h->walk = u->mark;
				long long val = sec ? h->time : file ? h->save : h->seq;
				if (round == 1 && !(file && val == 0)
				    && (step < 0 ? h->seq <= u->seq_cur : h->seq > u->seq_cur)
				    && (sec && val == closest
				        ? (step < 0 ? h->seq < closest_seq : h->seq > closest_seq)
				        : closest == closest_start || dist(val, target) <= dist(closest, target))) {
					closest = val;
					closest_seq = h->seq;
				}
				/* by time, keep looking for the best seq */
				if (target == val && !sec) {
					target = h->seq;
					break;
				}
				if (h->prev && h->prev->walk != u->nomark && h->prev->walk != u->mark)
					h = h->prev;
				else if (h->alt_next && h->alt_next->walk != u->nomark && h->alt_next->walk != u->mark)
					h = h->alt_next;
				else if (h->next && !h->alt_prev && h->next->walk != u->nomark && h->next->walk != u->mark) {
					if (h == u->curhead) h->walk = u->nomark;   /* still at the start: not through it */
					h = h->next;
				} else {                                     /* backtrack */
					h->walk = u->nomark;
					h = h->alt_prev ? h->alt_prev : h->next;
				}
			}
			if (h) break;
			if (absolute) return -1;
			if (closest == closest_start) return 0;
			target = closest_seq;
			sec = file = 0;
			if (step < 0) above = 1;      /* stop above the header */
		}
		if (!h) return 1;
	}
	u->target = target;
	u->above = above;
	u->phase = 1;
	return 1;
}

/* undo_time(): the walk, one group per call; first up the tree as far as
   needed, then down, branching off where needed */
int undo_step(Undo *u, Text *t, UndoWhere *w)
{
	if (u->phase == 1) {
		Head *h = u->curhead ? u->curhead->next : u->newhead;
		if (h && !(u->target > 0 && h->walk != u->mark) && !(h->seq == u->target && !u->above)) {
			u->curhead = h;
			if (undoredo(u, t, 1, w)) return -1;
			if (u->target > 0) h->walk = u->nomark;   /* don't go back down here */
			return 1;
		}
		u->phase = u->target > 0 ? 2 : 0;   /* back at the origin: no redo */
	}
	if (u->phase != 2) return 0;
	u->phase = 0;
	Head *h = u->curhead;
	if (!h) return 0;
	/* the last marked alternative is the one: make it the first, so that u
	   and Ctrl-R take it */
	Head *last = h;
	while (last->alt_next && last->alt_next->walk == u->mark) last = last->alt_next;
	if (last != h) {
		if (last->alt_next) last->alt_next->alt_prev = last->alt_prev;
		last->alt_prev->alt_next = last->alt_next;
		last->alt_prev = NULL;
		last->alt_next = h;
		h->alt_prev = last;
		if (u->oldhead == h) u->oldhead = last;
		h = last;
		if (h->next) h->next->prev = h;
	}
	u->curhead = h;
	if (h->walk != u->mark) return 0;   /* reached the target */
	if (h->seq == u->target && u->above) {
		u->seq_cur = (long)u->target - 1;   /* Neovim's number, even off a branch */
		return 0;
	}
	if (undoredo(u, t, 0, w)) return -1;
	if (!h->prev) u->newhead = h;
	u->curhead = h->prev;
	if (h->seq != u->target && h->prev && h->prev->walk == u->mark) u->phase = 2;
	return 1;
}

/* u_update_save_nr(): the mark goes on the header "u" would undo */
void undo_saved(Undo *u)
{
	u->save_cur = ++u->save_last;
	Head *h = u->curhead ? u->curhead->next : u->newhead;
	if (h) h->save = u->save_last;
}

/* The undo file.  Neovim keys its undofile by the SHA-256 of the text
   (u_compute_hash) and refuses one written for another text; the format is
   our own, since Neovim's stores lines.  All numbers are 8 bytes, little
   endian; a header's links are the seq of the header they point to, 0 for
   none (put_header_ptr). */
static const char MAGIC[8] = "HomUndo1";

/* FIPS 180-4 SHA-256 */
static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n) { return x >> n | x << (32 - n); }

static void sha_block(uint32_t h[8], const unsigned char *p)
{
	uint32_t w[64], v[8];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 64; i++)
		w[i] = w[i - 16] + (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3)
		     + w[i - 7] + (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10);
	memcpy(v, h, sizeof v);
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = v[7] + (ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K256[i] + w[i];
		uint32_t t2 = (ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
		memmove(v + 1, v, 7 * sizeof *v);
		v[4] += t1;
		v[0] = t1 + t2;
	}
	for (int i = 0; i < 8; i++) h[i] += v[i];
}

static void sha256(const Text *t, unsigned char out[32])
{
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	unsigned char b[128];
	size_t len = text_len(t), at = 0, n;
	while ((n = text_read(t, at, (char *)b, 64)) == 64) {
		sha_block(h, b);
		at += 64;
	}
	memset(b + n, 0, sizeof b - n);
	b[n] = 0x80;
	size_t end = n < 56 ? 64 : 128;
	for (int i = 0; i < 8; i++) b[end - 1 - i] = (unsigned char)((unsigned long long)len * 8 >> 8 * i);
	sha_block(h, b);
	if (end == 128) sha_block(h, b + 64);
	for (int i = 0; i < 32; i++) out[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
}

static int put(FILE *f, unsigned long long v)
{
	unsigned char b[8];
	for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> 8 * i);
	return fwrite(b, 8, 1, f) == 1 ? 0 : -1;
}

static long sq(const Head *h) { return h ? h->seq : 0; }

/* every header below and after h (u_unch_branch's walk); returns how many */
static long put_branch(FILE *f, const Head *h)
{
	long n = 0;
	for (; h; h = h->prev, n++) {
		int r = put(f, (unsigned long long)h->seq) | put(f, (unsigned long long)sq(h->next))
		      | put(f, (unsigned long long)sq(h->prev)) | put(f, (unsigned long long)sq(h->alt_next))
		      | put(f, (unsigned long long)sq(h->alt_prev)) | put(f, (unsigned long long)h->time)
		      | put(f, (unsigned long long)h->save) | put(f, h->cur.line) | put(f, h->cur.col)
		      | put(f, h->cur.flags) | put(f, h->after) | put(f, h->n);
		for (size_t i = 0; i < h->n; i++) {
			const Edit *e = &h->e[i];
			r |= put(f, e->pos) | put(f, e->ndel) | put(f, e->nins);
			if (e->ndel + e->nins && fwrite(e->b, e->ndel + e->nins, 1, f) != 1) r = -1;
		}
		long m = r ? -1 : h->alt_next ? put_branch(f, h->alt_next) : 0;
		if (m < 0) return -1;
		n += m;
	}
	return n;
}

int undo_write(const Undo *u, const Text *t, const char *path)
{
	unsigned char hash[32];
	char tmp[4096];
	if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path) >= sizeof tmp) { errno = ENAMETOOLONG; return -1; }
	sha256(t, hash);
	FILE *f = fopen(tmp, "wb");
	if (!f) return -1;
	int r = -(fwrite(MAGIC, 8, 1, f) != 1 || fwrite(hash, 32, 1, f) != 1);
	r |= put(f, text_len(t)) | put(f, (unsigned long long)u->numhead)
	   | put(f, (unsigned long long)u->seq_last) | put(f, (unsigned long long)u->seq_cur)
	   | put(f, (unsigned long long)u->save_last) | put(f, (unsigned long long)u->save_cur)
	   | put(f, (unsigned long long)u->time_cur) | put(f, (unsigned long long)sq(u->newhead))
	   | put(f, (unsigned long long)sq(u->curhead)) | put(f, (unsigned long long)sq(u->oldhead));
	if (!r && put_branch(f, u->oldhead) != u->numhead) {
		errno = EIO;
		r = -1;
	}
	/* Neovim fsyncs the undofile too ('fsync') */
	if (fflush(f) || fsync(fileno(f))) r = -1;
	if (fclose(f)) r = -1;
	if (!r && rename(tmp, path)) r = -1;
	if (r) {
		int e = errno;
		remove(tmp);
		errno = e;
	}
	return r;
}

typedef struct { const unsigned char *p, *end; } In;

static int get(In *in, unsigned long long *v)
{
	if (in->end - in->p < 8) return -1;
	*v = 0;
	for (int i = 0; i < 8; i++) *v |= (unsigned long long)in->p[i] << 8 * i;
	in->p += 8;
	return 0;
}

static int byseq(const void *a, const void *b)
{
	long x = (*(Head *const *)a)->seq, y = (*(Head *const *)b)->seq;
	return (x > y) - (x < y);
}

/* the header numbered s, NULL for 0; *bad when there is none */
static Head *find(Head **hs, long n, unsigned long long s, int *bad)
{
	if (!s) return NULL;
	Head key = { .seq = (long)s }, *k = &key, **h = s > (unsigned long long)LONG_MAX ? NULL : bsearch(&k, hs, (size_t)n, sizeof *hs, byseq);
	if (!h) *bad = 1;
	return h ? *h : NULL;
}

/* the links make one tree, each header met once (marked in walk) */
static long check_links(Head *h)
{
	long n = 0;
	for (; h; h = h->prev, n++) {
		if (h->walk || (h->prev && (h->prev->next != h || h->prev->alt_prev))
		    || (h->alt_next && (h->alt_next->alt_prev != h || h->alt_next->next != h->next)))
			return -1;
		h->walk = 1;
		long m = h->alt_next ? check_links(h->alt_next) : 0;
		if (m < 0) return -1;
		n += m;
	}
	return n;
}

/* replace from by to at pos, only if the text holds from there */
static int swap(Text *t, size_t pos, const char *from, size_t nf, const char *to, size_t nt)
{
	char b[256];
	size_t len = text_len(t);
	if (pos > len || nf > len - pos) return -1;
	for (size_t i = 0; i < nf; i += sizeof b) {
		size_t k = nf - i < sizeof b ? nf - i : sizeof b;
		if (text_read(t, pos + i, b, k) != k || memcmp(b, from + i, k)) return -1;
	}
	return text_delete(t, pos, nf) || text_insert(t, pos, to, nt) ? -1 : 0;
}

static int apply(Text *t, const Head *h, int undo)
{
	if (undo) {
		for (size_t i = h->n; i-- > 0;)
			if (swap(t, h->e[i].pos, h->e[i].b + h->e[i].ndel, h->e[i].nins, h->e[i].b, h->e[i].ndel)) return -1;
	} else
		for (size_t i = 0; i < h->n; i++)
			if (swap(t, h->e[i].pos, h->e[i].b, h->e[i].ndel, h->e[i].b + h->e[i].ndel, h->e[i].nins)) return -1;
	return 0;
}

/* every change in the tree applies to the text of its parent state */
static int check_text(Text *t, const Head *h)
{
	for (; h; h = h->alt_next)
		if (apply(t, h, 0) || check_text(t, h->prev) || apply(t, h, 1)) return -1;
	return 0;
}

static void free_heads(Head **hs, unsigned long long n)
{
	for (unsigned long long i = 0; i < n && hs[i]; i++) {
		for (size_t j = 0; j < hs[i]->n; j++) free(hs[i]->e[j].b);
		free(hs[i]->e);
		free(hs[i]);
	}
}

/* the headers, in file order, with their links (next, prev, alt_next,
   alt_prev) still as numbers in ln; 0 ok */
static int get_heads(In *in, Head **hs, unsigned long long (*ln)[4], unsigned long long nh, long seq_last)
{
	for (unsigned long long i = 0; i < nh; i++) {
		unsigned long long w[12];
		Head *h = hs[i] = calloc(1, sizeof *h);
		if (!h) return -1;
		for (int j = 0; j < 12; j++) if (get(in, &w[j])) return -1;
		if (!w[0] || w[0] > (unsigned long long)seq_last || w[11] > (size_t)(in->end - in->p) / 24
		    || !(h->e = malloc((w[11] ? w[11] : 1) * sizeof *h->e)))
			return -1;
		h->seq = (long)w[0];
		memcpy(ln[i], w + 1, sizeof ln[i]);
		h->time = (long long)w[5];
		h->save = (long)(w[6] & LONG_MAX);
		h->cur = (UndoCursor){ (size_t)w[7], (size_t)w[8], (unsigned)w[9] };
		h->after = (unsigned)w[10];
		for (h->cap = w[11]; h->n < h->cap; h->n++) {
			unsigned long long a, d, s;
			if (get(in, &a) || get(in, &d) || get(in, &s) || d > (size_t)(in->end - in->p)
			    || s > (size_t)(in->end - in->p) - d)
				return -1;
			char *b = malloc(d + s ? d + s : 1);
			if (!b) return -1;
			memcpy(b, in->p, d + s);
			in->p += d + s;
			h->e[h->n] = (Edit){ a, d, s, b };
		}
	}
	return 0;
}

/* Neovim checks the hash and the magic numbers (u_read_undo); this also
   checks that the links make one tree and that every change applies to its
   parent's text, so a damaged file is refused instead of corrupting text */
Undo *undo_read(const Text *t, const char *path)
{
	unsigned char *buf = NULL, hash[32];
	size_t len = 0, cap = 0, n;
	unsigned long long nh = 0, tl, v[8];
	Undo *u = NULL;
	Head **hs = NULL, **ss = NULL;
	unsigned long long (*ln)[4] = NULL;
	Text *c = NULL;
	int bad = 1;
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	do {
		if (len == cap) {
			unsigned char *b = realloc(buf, cap = cap ? 2 * cap : 1 << 16);
			if (!b) goto out;
			buf = b;
		}
		len += n = fread(buf + len, 1, cap - len, f);
	} while (n);
	In in = { buf + 40, buf + len };
	sha256(t, hash);
	if (len < 40 || memcmp(buf, MAGIC, 8) || memcmp(buf + 8, hash, 32)
	    || get(&in, &tl) || tl != text_len(t) || get(&in, &nh) || nh > UNDO_LEVELS + 1)
		goto out;
	for (int i = 0; i < 8; i++) if (get(&in, &v[i])) goto out;
	if (!(u = undo_new()) || !(hs = calloc(nh + 1, sizeof *hs)) || !(ss = calloc(nh + 1, sizeof *ss))
	    || !(ln = calloc(nh + 1, sizeof *ln)))
		goto out;
	u->seq_last = (long)(v[0] & LONG_MAX);
	u->seq_cur = (long)(v[1] & LONG_MAX);
	u->save_last = (long)(v[2] & LONG_MAX);
	u->save_cur = (long)(v[3] & LONG_MAX);
	u->time_cur = (long long)v[4];
	u->numhead = (long)nh;
	if (get_heads(&in, hs, ln, nh, u->seq_last) || in.p != in.end) goto out;
	memcpy(ss, hs, nh * sizeof *hs);
	qsort(ss, nh, sizeof *ss, byseq);
	int miss = 0;
	for (unsigned long long i = 0; i + 1 < nh; i++) miss |= ss[i]->seq == ss[i + 1]->seq;
	for (unsigned long long i = 0; i < nh; i++) {
		hs[i]->next = find(ss, (long)nh, ln[i][0], &miss);
		hs[i]->prev = find(ss, (long)nh, ln[i][1], &miss);
		hs[i]->alt_next = find(ss, (long)nh, ln[i][2], &miss);
		hs[i]->alt_prev = find(ss, (long)nh, ln[i][3], &miss);
	}
	u->newhead = find(ss, (long)nh, v[5], &miss);
	u->curhead = find(ss, (long)nh, v[6], &miss);
	u->oldhead = find(ss, (long)nh, v[7], &miss);
	if (miss || (u->oldhead && (u->oldhead->next || u->oldhead->alt_prev)) || (u->curhead && u->curhead->alt_prev)
	    || check_links(u->oldhead) != (long)nh || (nh && !u->newhead && !u->curhead))
		goto out;
	for (unsigned long long i = 0; i < nh; i++) hs[i]->walk = 0;
	/* from the current state up to the root, then the whole tree down */
	char *b = malloc(tl ? tl : 1);
	if (!b) goto out;
	c = text_new(b, text_read(t, 0, b, tl));
	free(b);
	if (!c) goto out;
	bad = 0;
	for (Head *h = u->curhead ? u->curhead->next : u->newhead; h && !bad; h = h->next) bad = apply(c, h, 1);
	if (!bad) bad = check_text(c, u->oldhead);
out:
	if (f) fclose(f);
	text_free(c);
	free(buf);
	if (bad && hs) free_heads(hs, nh);
	if (bad && u) {
		u->oldhead = NULL;
		undo_free(u);
		u = NULL;
	}
	free(hs);
	free(ss);
	free(ln);
	return u;
}
