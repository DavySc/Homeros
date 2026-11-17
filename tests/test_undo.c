/* undo.h: Undo.tla's properties as tests on random edit groups.
   Undo lands exactly on the text before the group, redo on the text after;
   undo closes an open group first; a new edit clears redo. */
#include <string.h>
#include "check.h"
#include "undo.h"

enum { G = 40, CAP = 4096 };
static char snap[G + 1][CAP];
static size_t snapn[G + 1];

static void read_all(const Text *t, char *b, size_t *n) { *n = text_read(t, 0, b, CAP); }
static int is(const Text *t, int k) { char b[CAP]; size_t n; read_all(t, b, &n); return n == snapn[k] && !memcmp(b, snap[k], n); }

static void edit(Text *t, Undo *u)
{
	size_t len = text_len(t), pos = rnd((unsigned)len + 1), nd = rnd((unsigned)(len - pos < 4 ? len - pos : 4) + 1);
	char del[4], ins[4] = "xy\nz";
	size_t ni = rnd(5);
	if (len > CAP - 8) ni = 0;
	text_read(t, pos, del, nd);
	CHECK(!text_delete(t, pos, nd) && !text_insert(t, pos, ins, ni));
	CHECK(!undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ 0, 0, 0 }, 0));
}

int main(void)
{
	for (int round = 0; round < 300; round++) {
		Text *t = text_new("hello\nworld\n", 12);
		Undo *u = undo_new();
		UndoWhere w;
		read_all(t, snap[0], &snapn[0]);
		int groups = 1 + (int)rnd(G);
		for (int g = 1; g <= groups; g++) {
			for (int e = 1 + (int)rnd(4); e > 0; e--) edit(t, u);
			if (rnd(2)) undo_close(u);          /* left open: undo must close it */
			read_all(t, snap[g], &snapn[g]);
			CHECK(undo_seq(u) == (unsigned long)g);
			if (g < groups) undo_close(u);
		}
		for (int g = groups; g > 0; g--) { CHECK(undo_undo(u, t, &w) == 1); CHECK(is(t, g - 1)); CHECK(w.lo <= w.hi && w.hi <= text_len(t)); }
		CHECK(undo_undo(u, t, &w) == 0 && undo_seq(u) == 0);
		for (int g = 1; g <= groups; g++) { CHECK(undo_redo(u, t, &w) == 1); CHECK(is(t, g)); }
		CHECK(undo_redo(u, t, &w) == 0);
		/* undo half, edit: the redo part is gone and numbers keep growing */
		int back = (int)rnd((unsigned)groups) + 1;
		for (int i = 0; i < back; i++) CHECK(undo_undo(u, t, &w) == 1);
		edit(t, u);
		undo_close(u);
		CHECK(undo_seq(u) == (unsigned long)groups + 1);
		CHECK(undo_redo(u, t, &w) == 0);
		CHECK(undo_undo(u, t, &w) == 1 && is(t, groups - back));
		undo_free(u);
		text_free(t);
	}
	/* undo with a group open closes it first (Undo.tla noclose): the next
	   edit is a new group, and undoing it gives the text from before it */
	{
		Text *t = text_new("abc", 3);
		Undo *u = undo_new();
		UndoWhere w;
		UndoCursor c = { 0, 0, 0 };
		CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, c, 0));
		CHECK(undo_undo(u, t, &w) == 1);
		CHECK(!text_insert(t, 3, "y", 1) && !undo_record(u, 3, "", 0, "y", 1, c, 0));
		CHECK(undo_seq(u) == 2);
		CHECK(undo_undo(u, t, &w) == 1 && text_len(t) == 3 && undo_undo(u, t, &w) == 0);
		undo_free(u);
		text_free(t);
	}
	puts("test_undo: 300 rounds of random groups; undo, redo and redo-clearing exact");
	return 0;
}
