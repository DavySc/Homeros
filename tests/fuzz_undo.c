/* libFuzzer: the undo tree under random edits, u, Ctrl-R, writes, clock
   steps, every kind of move and undo-file round trips.  Each state's text is
   recorded when it is made; u, Ctrl-R and :undo N must land on exactly that
   text (UndoTree.tla Exact), no move may fail, the span stays in the text,
   and a tree read back from its file is the one written.  :earlier by time
   or writes can leave Neovim's state number one off a branch (undo_time()
   sets seq_cur = target - 1), so after those the number is trusted again
   only once u or Ctrl-R has moved.  The hook must see every change undo and
   redo make, before it is made: a mirror kept only from what it reports
   equals the text at each call and after each op. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "undo.h"

static time_t now = 1700000000;
time_t time(time_t *t) { if (t) *t = now; return now; }

enum { MAXS = 256, CAP = 4096 };
static char snap[MAXS + 1][CAP], b[CAP];
static size_t snapn[MAXS + 1];

static char mir[CAP];
static size_t mirn;

static void hook(void *ctx, const Text *t, size_t pos, size_t nd, const char *ins, size_t ni)
{
	(void)ctx;
	size_t n = text_read(t, 0, b, CAP);
	if (n != mirn || memcmp(b, mir, n) || pos > n || nd > n - pos || n - nd + ni > CAP) abort();
	memmove(mir + pos + ni, mir + pos + nd, n - pos - nd);
	memcpy(mir + pos, ins, ni);
	mirn = n - nd + ni;
}

static int at_snap(const Text *t, unsigned long s)
{
	size_t n = text_read(t, 0, b, CAP);
	return s <= MAXS && n == snapn[s] && !memcmp(b, snap[s], n);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char path[64];
	if (!*path) snprintf(path, sizeof path, "/tmp/homeros-fuzz-undo-%d", (int)getpid());
	Text *t = text_new("ab\ncd\n", 6);
	Undo *u = undo_new();
	if (!t || !u) abort();
	undo_hook(u, hook, NULL);
	mirn = text_read(t, 0, mir, CAP);
	unsigned long seqs = 0;
	int known = 1;
	snapn[0] = text_read(t, 0, snap[0], CAP);
	for (size_t i = 0; i + 2 <= n; i += 2) {
		unsigned op = d[i] % 12, a = d[i + 1];
		UndoWhere w;
		int r = 0, moved = 0;
		if (op < 3) {   /* an edit, which may join the open group */
			size_t len = text_len(t), pos = a % (len + 1), nd = (a >> 4) % 3;
			char del[3], ins[4] = "x\nyz";
			size_t ni = len > CAP - 8 ? 0 : a % 5;
			if (nd > len - pos) nd = len - pos;
			text_read(t, pos, del, nd);
			if (text_delete(t, pos, nd) || text_insert(t, pos, ins, ni)
			    || undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ a, 0, 0 }, 0))
				abort();
			if (undo_seq(u) > seqs) seqs = undo_seq(u);
			if (seqs <= MAXS) snapn[seqs] = text_read(t, 0, snap[seqs], CAP);   /* the group's text so far */
			if (op == 0) undo_close(u);
			mirn = text_read(t, 0, mir, CAP);
			continue;
		}
		switch (op) {
		case 3: r = undo_undo(u, t, &w); moved = r; break;
		case 4: r = undo_redo(u, t, &w); moved = r; break;
		case 5: undo_saved(u); break;
		case 6: now += a % 4; break;
		case 7: r = undo_time(u, (long)(a % 9) - 4, UNDO_SEQ); break;
		case 8: r = undo_time(u, (long)(a % 7) - 3, UNDO_SEC); break;
		case 9: r = undo_time(u, (long)(a % 5) - 2, UNDO_FILE); break;
		case 10: r = undo_time(u, (long)(a % (seqs + 2)), UNDO_ABS); break;
		default: {   /* write the file, read it back, go on with the copy */
			undo_close(u);
			if (undo_write(u, t, path)) abort();
			Undo *v = undo_read(t, path);
			if (!v) abort();
			undo_free(u);
			u = v;
			undo_hook(u, hook, NULL);
			break;
		}
		}
		if (r < 0 && op != 10) abort();
		if (op >= 7 && op <= 9 && r > 0) known = 0;
		if (moved > 0) known = 1;
		if (op >= 7 && op <= 10 && r > 0) {
			while ((r = undo_step(u, t, &w)) > 0)
				if (w.lo > w.hi || w.hi > text_len(t)) abort();
			if (r) abort();
		} else if (r > 0 && (w.lo > w.hi || w.hi > text_len(t))) abort();
		if ((op == 3 || op == 4 || op == 10) && known && !at_snap(t, undo_seq(u))) abort();
		if (text_read(t, 0, b, CAP) != mirn || memcmp(b, mir, mirn)) abort();
	}
	undo_free(u);
	text_free(t);
	return 0;
}
