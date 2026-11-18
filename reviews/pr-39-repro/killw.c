/* Review of PR 39: the undo file is written over and over to one name; the
   process is killed at a random moment; "read" then says what is at the name.
   The text is "" at every write (an insert and its delete), so any whole
   file reads back.  Usage: killw.exe write | killw.exe read */
#include <stdio.h>
#include <string.h>
#include "undo.h"
int main(int argc, char **argv)
{
	static const char path[] = "kill.un";
	Text *t = text_new("", 0);
	if (argc > 1 && !strcmp(argv[1], "read")) {
		FILE *f = fopen(path, "rb");
		if (!f) { puts("missing"); return 2; }
		fclose(f);
		Undo *u = undo_read(t, path);
		if (!u) { puts("unreadable"); return 3; }
		printf("ok seq %lu\n", undo_seq(u));
		return 0;
	}
	Undo *u = undo_new();
	for (;;) {
		text_insert(t, 0, "x", 1);
		undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0);
		undo_close(u);
		text_delete(t, 0, 1);
		undo_record(u, 0, "x", 1, "", 0, (UndoCursor){ 0, 0, 0 }, 0);
		undo_close(u);
		if (undo_write(u, t, path)) { puts("write failed"); fflush(stdout); return 1; }
	}
}
