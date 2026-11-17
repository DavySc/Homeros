/* Review of package 2: what UndoWhere promises (undo.h) and no test checks.
   For every group undone or redone (u, Ctrl-R and each undo_step, also after
   a round trip through the undo file): bytes outside [lo, hi) of the result
   equal the text before; cur is the cursor given when the group opened;
   flags are that cursor's flags after an undo and the group's last "after"
   after a redo.  The group is named by its cursor (line = its seq). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "undo.h"
enum { CAP = 1 << 15, MAXG = 4096 };
static unsigned aft[MAXG];
static char B[CAP], A[CAP];
static size_t bn;
static long nchk, ntight;
static void before(const Text *t) { bn = text_read(t, 0, B, CAP); }
static void after_group(const Text *t, const Undo *u, const UndoWhere *w)
{
	size_t an = text_read(t, 0, A, CAP);
	CHECK(w->lo <= w->hi && w->hi <= an);
	CHECK(w->lo <= bn && !memcmp(A, B, w->lo));
	size_t tail = an - w->hi;
	CHECK(tail <= bn - w->lo && !memcmp(A + w->hi, B + bn - tail, tail));
	/* is the span the smallest one?  not promised; counted */
	size_t p = 0; while (p < an && p < bn && A[p] == B[p]) p++;
	if (p == w->lo || (an == bn && !memcmp(A, B, an))) ntight++;
	size_t g = w->cur.line;
	CHECK(g >= 1 && g < MAXG && w->cur.col == g * 3 % 11);
	int redo = undo_seq(u) == g;
	if (redo) CHECK(w->flags == aft[g]); else CHECK(w->flags == w->cur.flags && w->cur.flags == g % 2);
	nchk++;
}
static void edit(Text *t, Undo *u)
{
	size_t len = text_len(t), pos = rnd((unsigned)len + 1), nd = rnd((unsigned)(len - pos < 3 ? len - pos : 3) + 1);
	char del[4], ins[] = "ab\ncd";
	size_t ni = len > CAP - 8 ? 0 : rnd(6);
	text_read(t, pos, del, nd);
	CHECK(!text_delete(t, pos, nd) && !text_insert(t, pos, ins, ni));
	/* the cursor is only used if this edit opens a group; give the number the group would get */
	static unsigned long last; static int open;
	(void)open;
	unsigned long g = last + 1;
	unsigned a = rnd(4);
	CHECK(!undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ g, g * 3 % 11, g % 2 }, a));
	unsigned long s = undo_seq(u);
	CHECK(s < MAXG);
	if (s == g) last = g;      /* opened group g */
	aft[s] = a;
}
int main(int argc, char **argv)
{
	char path[] = "/tmp/r2-span-XXXXXX";
	close(mkstemp(path));
	if (argc > 1) rng ^= strtoull(argv[1], 0, 10) * 0x9e3779b97f4a7c15ULL;
	Text *t = text_new("hello\nworld\n", 12);
	Undo *u = undo_new();
	UndoWhere w;
	/* u and Ctrl-R only move one group and set seq to it (redo) or its parent
	   (undo); time moves can leave seq one off (the quirk), so the redo/undo
	   test above is only used where seq is exact: u, Ctrl-R, :undo N */
	for (int i = 0; i < 3000; i++) {
		int r;
		switch (rnd(8)) {
		case 0: case 1: case 2: edit(t, u); if (rnd(3)) undo_close(u); break;
		case 3: before(t); if (undo_undo(u, t, &w) == 1) after_group(t, u, &w); break;
		case 4: before(t); if (undo_redo(u, t, &w) == 1) after_group(t, u, &w); break;
		case 5: r = undo_time(u, (long)rnd((unsigned)undo_seq(u) + 3), UNDO_ABS);
			if (r > 0) for (;;) { before(t); r = undo_step(u, t, &w); CHECK(r >= 0); if (!r) break;
				/* inside a move seq follows each group as for u / Ctrl-R */
				after_group(t, u, &w); }
			break;
		case 6: undo_saved(u); break;
		default: {
			undo_close(u);
			CHECK(!undo_write(u, t, path));
			Undo *v = undo_read(t, path);
			CHECK(v);
			undo_free(u); u = v;
		}
		}
		if (undo_seq(u) > MAXG - 8) break;
	}
	printf("span: %ld groups checked (span covers the change, cursor and flags right); span starts at the first changed byte in %ld\n", nchk, ntight);
	undo_free(u); text_free(t); remove(path);
	return 0;
}
