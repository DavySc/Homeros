/* The undo file where the system differs (src/undo/build.mk also builds this
   for Windows and runs it under Wine): writing over an existing undo file,
   which Windows' rename() refuses, then reading it back; and counts at the
   ends of long's range, 32 bits there. */
#include <limits.h>
#include "check.h"
#include "undo.h"

int main(void)
{
	static const char path[] = "test_undo_win.un";
	Text *t = text_new("", 0);
	Undo *u = undo_new();
	UndoWhere w;
	CHECK(t && u);
	for (int i = 0; i < 3; i++) {
		CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0));
		undo_close(u);
		CHECK(!undo_write(u, t, path));
	}
	Undo *v = undo_read(t, path);
	CHECK(v);
	int n = 0;
	while (undo_undo(v, t, &w) == 1) n++;
	CHECK(n == 3 && text_len(t) == 0);
	CHECK(undo_time(v, LONG_MAX, UNDO_SEQ) == 1);
	while (undo_step(v, t, &w) > 0) {}
	CHECK(undo_seq(v) == 3 && text_len(t) == 3);
	CHECK(undo_time(v, LONG_MIN, UNDO_SEQ) == 1);
	while (undo_step(v, t, &w) > 0) {}
	CHECK(undo_seq(v) == 0 && text_len(t) == 0);
	remove(path);
	undo_free(v);
	undo_free(u);
	text_free(t);
	printf("test_undo_win: an undo file written over three times and read back; counts at long's ends\n");
	return 0;
}
