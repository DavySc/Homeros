/* Review of package 2, finding 2: a file whose newhead is not the newest
   change of its line is accepted when the edits happen to apply (here: three
   times "x" typed at the start), and the next edit then cuts the newer
   changes off the tree (leaked, and counted in numhead for ever).  The fact
   undo.c relies on in open_group() -- with no curhead, newhead has no prev
   and is the first alternative -- is checked on 200 x 400 random ops first
   (it holds in memory), then the damaged file is read.
   Includes undo.c to look inside. */
#include "../../src/undo/undo.c"
#include "check.h"

static long walk3(const Head *h)
{
	long n = 0;
	for (; h; h = h->prev, n++) {
		CHECK(!h->prev || !h->prev->alt_prev);
		if (h->alt_next) n += walk3(h->alt_next);
	}
	return n;
}
static void edit(Text *t, Undo *u)
{
	size_t len = text_len(t), pos = rnd((unsigned)len + 1), nd = rnd((unsigned)(len - pos < 3 ? len - pos : 3) + 1);
	char del[4], ins[] = "ab\ncd";
	size_t ni = len > 4000 ? 0 : rnd(6);
	text_read(t, pos, del, nd);
	CHECK(!text_delete(t, pos, nd) && !text_insert(t, pos, ins, ni));
	CHECK(!undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ 0, 0, 0 }, 0));
}
int main(void)
{
	UndoWhere w;
	long held = 0, facts = 0;
	for (int round = 0; round < 200; round++) {
		Text *t = text_new("hello\n", 6);
		Undo *u = undo_new();
		for (int i = 0; i < 400; i++) {
			int r = 0, k = (int)rnd(9);
			switch (k) {
			case 0: case 1: edit(t, u); if (rnd(3)) undo_close(u); break;
			case 2: undo_undo(u, t, &w); break;
			case 3: undo_redo(u, t, &w); break;
			case 4: undo_saved(u); break;
			case 5: r = undo_time(u, (long)rnd(9) - 4, UNDO_SEQ); break;
			case 6: r = undo_time(u, (long)rnd(5) - 2, UNDO_FILE); break;
			case 7: r = undo_time(u, (long)rnd((unsigned)undo_seq(u) + 3), UNDO_ABS); break;
			default: r = undo_time(u, (long)rnd(5) - 2, UNDO_SEC); break;
			}
			if (k >= 5 && r > 0) while (undo_step(u, t, &w) > 0) {}
			/* the three facts undo.c's Head comment relies on */
			CHECK(!u->curhead || !u->curhead->alt_prev);
			CHECK(!u->oldhead || (!u->oldhead->alt_prev && !u->oldhead->next));
			facts += walk3(u->oldhead);
			if (!u->curhead && u->newhead) { CHECK(!u->newhead->prev && !u->newhead->alt_prev); held++; }
			CHECK(u->seq_cur <= u->seq_last && u->save_cur <= u->save_last);
		}
		undo_free(u); text_free(t);
	}
	printf("in memory: with no curhead, newhead is a leaf and the first alternative (%ld states); the three facts hold (%ld headers looked at)\n", held, facts);

	char path[] = "/tmp/r2-newhead-XXXXXX";
	close(mkstemp(path));
	Text *t = text_new("", 0);
	Undo *u = undo_new();
	for (int i = 0; i < 3; i++) {
		CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0));
		undo_close(u);
	}
	CHECK(!undo_write(u, t, path));
	FILE *f = fopen(path, "r+b");
	CHECK(f && !fseek(f, 96, SEEK_SET) && fgetc(f) == 3);   /* newhead: change 3 */
	CHECK(!fseek(f, 96, SEEK_SET) && fputc(1, f) == 1 && !fclose(f));   /* one byte: change 1 */
	Undo *d = undo_read(t, path);
	remove(path);
	if (!d) { puts("damaged file refused"); undo_free(u); text_free(t); return 0; }
	printf("damaged file ACCEPTED: newhead is change %ld, which has a newer change %ld below it; %ld changes\n",
	       d->newhead->seq, d->newhead->prev->seq, d->numhead);
	CHECK(!text_insert(t, 0, "y", 1) && !undo_record(d, 0, "", 0, "y", 1, (UndoCursor){ 0, 0, 0 }, 0));
	undo_close(d);
	int n = 0;
	while (undo_undo(d, t, &w) == 1) n++;
	char b[8] = "";
	text_read(t, 0, b, 7);
	printf("after one edit: numhead %ld, but u can undo only %d; the oldest text is now \"%s\" (was \"\")\n", d->numhead, n, b);
	undo_free(d); undo_free(u); text_free(t);   /* LeakSanitizer reports changes 2 and 3 */
	return 0;
}
