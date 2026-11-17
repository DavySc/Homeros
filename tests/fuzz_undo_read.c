/* libFuzzer on undo_read() with arbitrary file bodies.  Input: 1 byte text
   length, the text, then the file after its hash; the harness puts the right
   magic and hash in front, so the body is what undo_read() has to judge.  If
   the file is accepted, it reads back as written, and then no move fails
   and nothing overflows or leaks.  From the review
   of package 2 (reviews/r2/fuzz_read.c, which also writes a seed corpus). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "undo.h"

static time_t now = 1700000000;
time_t time(time_t *t) { if (t) *t = now; return now; }
static unsigned long long rng;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }

static void edit(Text *t, Undo *u)
{
	size_t len = text_len(t), pos = rnd((unsigned)len + 1), nd = rnd((unsigned)(len - pos < 3 ? len - pos : 3) + 1);
	char del[4], ins[] = "ab\ncd";
	size_t ni = len > 4000 ? 0 : rnd(6);
	text_read(t, pos, del, nd);
	if (text_delete(t, pos, nd) || text_insert(t, pos, ins, ni)
	    || undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ 0, 0, 0 }, 0)) abort();
}
static void op(Text *t, Undo *u, unsigned k)
{
	UndoWhere w;
	int r = 0;
	switch (k % 10) {
	case 0: case 1: edit(t, u); if (rnd(3)) undo_close(u); break;
	case 2: r = undo_undo(u, t, &w); break;
	case 3: r = undo_redo(u, t, &w); break;
	case 4: undo_saved(u); break;
	case 5: r = undo_time(u, (long)rnd(9) - 4, UNDO_SEQ); break;
	case 6: r = undo_time(u, (long)rnd(5) - 2, UNDO_FILE); break;
	case 7: r = undo_time(u, (long)rnd((unsigned)undo_seq(u) % 64 + 3), UNDO_ABS); break;
	case 8: now += rnd(3); break;
	default: r = undo_time(u, (long)rnd(5) - 2, UNDO_SEC); break;
	}
	if (r < 0 && k % 10 != 7) abort();
	if (k % 10 >= 5 && k % 10 != 8 && r > 0) {
		while ((r = undo_step(u, t, &w)) > 0) {}
		if (r) abort();
	}
}

static char path[64];
static unsigned char head[40];

static void hash_of(const Text *t)
{
	Undo *e = undo_new();
	if (undo_write(e, t, path)) abort();
	FILE *f = fopen(path, "rb");
	if (fread(head, 1, 40, f) != 40) abort();
	fclose(f);
	undo_free(e);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!*path) snprintf(path, sizeof path, "/tmp/homeros-fuzz-undo-read-%d", (int)getpid());
	if (n < 1 || n < 1u + d[0]) return 0;
	size_t tl = d[0];
	Text *t = text_new((const char *)d + 1, tl);
	if (!t) abort();
	hash_of(t);
	FILE *f = fopen(path, "wb");
	fwrite(head, 1, 40, f);
	fwrite(d + 1 + tl, 1, n - 1 - tl, f);
	fclose(f);
	Undo *u = undo_read(t, path);
	if (u) {
		rng = 88172645463325252ULL;
		for (size_t i = 0; i < n; i++) rng = rng * 31 + d[i] + 1;
		if (!rng) rng = 1;
		/* accepted: it must be writable and readable again as it is */
		if (undo_write(u, t, path)) abort();
		Undo *v = undo_read(t, path);
		if (!v) abort();
		undo_free(v);
		for (int i = 0; i < 60; i++) op(t, u, rnd(10));
		undo_close(u);
		/* written again; it may now be refused, as numbers read just under
		   the reader's bounds can have grown past them (fuzz_undo and
		   test_undo_file check that ordinary trees always read back) */
		if (undo_write(u, t, path)) abort();
		undo_free(undo_read(t, path));
		undo_free(u);
	}
	text_free(t);
	return 0;
}
