/* Review of package 2, second pass: undo_time() with a count at the end of
   long's range, as ":later 9223372036854775807" or a huge count before g+
   would give if the core passes the number on. */
#include <limits.h>
#include <stdio.h>
#include "undo.h"
int main(void)
{
	Text *t = text_new("", 0);
	Undo *u = undo_new();
	UndoWhere w;
	text_insert(t, 0, "x", 1);
	undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0);
	undo_close(u);
	undo_saved(u);
	static const int unit[] = { UNDO_SEQ, UNDO_SEC, UNDO_FILE, UNDO_ABS };
	static const long step[] = { LONG_MAX, LONG_MIN, LONG_MAX - 1, LONG_MIN + 1 };
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			int r = undo_time(u, step[j], unit[i]);
			if (r > 0) while (undo_step(u, t, &w) > 0) {}
			printf("unit %d step %ld: %d\n", unit[i], step[j], r);
		}
	undo_free(u); text_free(t);
	return 0;
}
