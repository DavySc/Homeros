/* Review of PR 39: the editor's paths are UTF-8 (src/file/win.c turns them
   into UTF-16); undo.c hands them to fopen and MoveFileExA, which read them
   in the ANSI code page.  Writes the undo file for "é.txt" and reads it back. */
#include <stdio.h>
#include "undo.h"
int main(void)
{
	static const char path[] = "\xc3\xa9.txt.un";
	Text *t = text_new("", 0);
	Undo *u = undo_new();
	int w = undo_write(u, t, path);
	Undo *v = undo_read(t, path);
	printf("write %d, read back %s\n", w, v ? "ok" : "NULL");
	return 0;
}
